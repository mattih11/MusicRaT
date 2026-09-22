#pragma once

#include <musicrat/backends/media/decoded_audio_pool.hpp>
#include <musicrat/dsp/varispeed_renderer.hpp>

#if MUSICRAT_HAS_RUBBERBAND
#include <musicrat/dsp/pitch_lock_processor.hpp>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>

namespace musicrat::backends::media {

template<std::size_t ChunkCount>
class DecodeAheadRenderer {
public:
    using Pool = DecodedAudioPool<ChunkCount>;
    using AudioBlock = CommRaT::Messages::AudioBlock;

    explicit DecodeAheadRenderer(
        Pool& pool,
        double output_sample_rate_hz = 0.0,
        bool pitch_lock = false)
        : pool_(pool) {
#if MUSICRAT_HAS_RUBBERBAND
        if (pitch_lock) {
            if (!std::isfinite(output_sample_rate_hz)
                || output_sample_rate_hz < 8000.0
                || output_sample_rate_hz > 192000.0) {
                throw std::invalid_argument(
                    "Pitch lock requires an output sample rate from 8000 to 192000 Hz");
            }
            pitch_lock_.emplace(static_cast<uint32_t>(
                std::llround(output_sample_rate_hz)));
            static_cast<void>(renderer_.set_rate_bounds(
                musicrat::dsp::PitchLockProcessor::minimum_rate,
                musicrat::dsp::PitchLockProcessor::maximum_rate));
        }
#else
        static_cast<void>(output_sample_rate_hz);
        if (pitch_lock) {
            throw std::invalid_argument(
                "Pitch lock requires a build with Rubber Band support");
        }
#endif
    }

    ~DecodeAheadRenderer() {
        release_all();
    }

    DecodeAheadRenderer(const DecodeAheadRenderer&) = delete;
    DecodeAheadRenderer& operator=(const DecodeAheadRenderer&) = delete;

    void prepare(
        uint64_t generation,
        double media_position = 0.0,
        uint16_t channel_count = 0) noexcept {
        release_all();
        output_channel_count_ = channel_count;
        renderer_.prepare(generation, media_position);
    #if MUSICRAT_HAS_RUBBERBAND
        if (pitch_lock_) {
            static_cast<void>(pitch_lock_->reset(renderer_.current_rate()));
        }
    #endif
    }

    musicrat::dsp::VarispeedRenderResult render_suspended(
        double output_sample_rate_hz,
        uint32_t requested_frames,
        uint64_t timestamp_ns,
        uint64_t sequence_number,
        AudioBlock& output,
        const CommRaT::Messages::DeckControlEventBlock* controls = nullptr) noexcept {
        release_all();
        return renderer_.render_underrun(
            output_channel_count_,
            output_sample_rate_hz,
            requested_frames,
            timestamp_ns,
            sequence_number,
            output,
            controls);
    }

    [[nodiscard]] bool set_rate_immediate(double rate) noexcept {
#if MUSICRAT_HAS_RUBBERBAND
        if (pitch_lock_ && !pitch_lock_->set_rate(rate)) {
            return false;
        }
#endif
        return renderer_.set_rate_immediate(rate);
    }

    [[nodiscard]] bool ramp_rate(double rate, uint32_t render_frames) noexcept {
#if MUSICRAT_HAS_RUBBERBAND
        if (pitch_lock_ && (rate < musicrat::dsp::PitchLockProcessor::minimum_rate
                || rate > musicrat::dsp::PitchLockProcessor::maximum_rate)) {
            return false;
        }
#endif
        return renderer_.ramp_rate(rate, render_frames);
    }

    [[nodiscard]] double current_rate() const noexcept {
        return renderer_.current_rate();
    }

    void set_playing(bool playing) noexcept {
        renderer_.set_playing(playing);
    }

    musicrat::dsp::VarispeedRenderResult render(
        double output_sample_rate_hz,
        uint32_t requested_frames,
        uint64_t timestamp_ns,
        uint64_t sequence_number,
        AudioBlock& output,
        const CommRaT::Messages::DeckControlEventBlock* controls = nullptr) noexcept {
        acquire_ready_chunks();
        std::array<musicrat::dsp::DecodedAudioChunkView, ChunkCount> views{};
        std::size_t view_count = 0;
        for (const auto& reservation : reservations_) {
            if (reservation) {
                views[view_count++] = reservation.chunk;
            }
        }

        musicrat::dsp::VarispeedRenderResult result{};
        if (view_count == 0) {
            result = renderer_.render_underrun(
                output_channel_count_,
                output_sample_rate_hz,
                requested_frames,
                timestamp_ns,
                sequence_number,
                output,
                controls);
        } else {
            output_channel_count_ = views[0].audio->channel_count;
#if MUSICRAT_HAS_RUBBERBAND
            std::span<double> rendered_rates{};
            if (pitch_lock_) {
                rendered_rates = std::span<double>{
                    rendered_rates_.data(), requested_frames};
            }
#endif
            result = renderer_.render(
                std::span<const musicrat::dsp::DecodedAudioChunkView>{
                    views.data(), view_count},
                output_sample_rate_hz,
                requested_frames,
                timestamp_ns,
                sequence_number,
                output,
                controls
#if MUSICRAT_HAS_RUBBERBAND
                , rendered_rates
#endif
            );
        }
    #if MUSICRAT_HAS_RUBBERBAND
        if (pitch_lock_ && output.channel_count > 0
            && (output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) == 0) {
                if ((output.flags
                        & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY) != 0) {
                    static_cast<void>(pitch_lock_->reset(renderer_.current_rate()));
                }
            if (view_count == 0) {
                static_cast<void>(pitch_lock_->process(
                    output, renderer_.current_rate(), output));
            } else {
                static_cast<void>(pitch_lock_->process(
                    output,
                    std::span<const double>{rendered_rates_.data(), requested_frames},
                    output));
            }
        }
    #endif
        release_consumed();
        return result;
    }

    [[nodiscard]] double media_position() const noexcept {
        return renderer_.media_position();
    }

    [[nodiscard]] uint64_t generation() const noexcept {
        return renderer_.generation();
    }

    [[nodiscard]] bool playing() const noexcept {
        return renderer_.playing();
    }

    [[nodiscard]] bool loop_enabled() const noexcept {
        return renderer_.loop_enabled();
    }

        [[nodiscard]] bool pitch_lock_enabled() const noexcept {
    #if MUSICRAT_HAS_RUBBERBAND
        return pitch_lock_.has_value();
    #else
        return false;
    #endif
        }

        [[nodiscard]] uint32_t algorithmic_latency_frames() const noexcept {
    #if MUSICRAT_HAS_RUBBERBAND
        return pitch_lock_ ? pitch_lock_->latency_frames() : 0;
    #else
        return 0;
    #endif
        }

        [[nodiscard]] double minimum_rate() const noexcept {
    #if MUSICRAT_HAS_RUBBERBAND
        return pitch_lock_ ? musicrat::dsp::PitchLockProcessor::minimum_rate
                   : musicrat::dsp::VarispeedRenderer::minimum_rate;
    #else
        return musicrat::dsp::VarispeedRenderer::minimum_rate;
    #endif
        }

        [[nodiscard]] double maximum_rate() const noexcept {
    #if MUSICRAT_HAS_RUBBERBAND
        return pitch_lock_ ? musicrat::dsp::PitchLockProcessor::maximum_rate
                   : musicrat::dsp::VarispeedRenderer::maximum_rate;
    #else
        return musicrat::dsp::VarispeedRenderer::maximum_rate;
    #endif
        }

private:
    void acquire_ready_chunks() noexcept {
        for (auto& reservation : reservations_) {
            if (reservation) {
                continue;
            }
            reservation = pool_.try_acquire_ready(renderer_.generation());
            if (!reservation) {
                break;
            }
        }
    }

    void release_consumed() noexcept {
        const double media_position = renderer_.retention_floor();
        for (auto& reservation : reservations_) {
            if (!reservation) {
                continue;
            }
            const double end = static_cast<double>(reservation.chunk.start_frame)
                + reservation.chunk.audio->frame_count;
            if (media_position >= end) {
                pool_.release(reservation);
                reservation = {};
            }
        }
    }

    void release_all() noexcept {
        for (auto& reservation : reservations_) {
            if (reservation) {
                pool_.release(reservation);
                reservation = {};
            }
        }
    }

    Pool& pool_;
    musicrat::dsp::VarispeedRenderer renderer_{};
#if MUSICRAT_HAS_RUBBERBAND
    std::optional<musicrat::dsp::PitchLockProcessor> pitch_lock_{};
    std::array<double, AudioBlock::MAX_FRAMES> rendered_rates_{};
#endif
    std::array<typename Pool::ReadReservation, ChunkCount> reservations_{};
    uint16_t output_channel_count_{0};
};

} // namespace musicrat::backends::media