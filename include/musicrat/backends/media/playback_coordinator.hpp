#pragma once

#include <musicrat/backends/media/decode_ahead.hpp>
#include <musicrat/backends/media/decode_ahead_renderer.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace musicrat::backends::media {

template<DecoderBackend Decoder, std::size_t ChunkCount>
class PlaybackCoordinator {
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);

public:
    using AudioBlock = CommRaT::Messages::AudioBlock;

    struct PlaybackDiagnostics {
        uint64_t generation{0};
        uint64_t duration_frames{0};
        uint64_t resident_frames{0};
        uint64_t buffered_start_frame{0};
        uint64_t buffered_end_frame{0};
        uint32_t source_sample_rate_hz{0};
        uint16_t source_channel_count{0};
        MediaCodec codec{MediaCodec::Unknown};
        DecodeAheadState worker_state{DecodeAheadState::Closed};
        DecodeError decode_error{DecodeError::None};
    };

    explicit PlaybackCoordinator(
        uint32_t chunk_frames = AudioBlock::MAX_FRAMES,
        std::size_t high_watermark = ChunkCount,
        double output_sample_rate_hz = 0.0,
        bool pitch_lock = false)
        : decode_ahead_(chunk_frames, high_watermark)
        , renderer_(decode_ahead_.pool(), output_sample_rate_hz, pitch_lock) {}

    [[nodiscard]] bool worker_open(const char* path) noexcept {
        begin_transition();
        const bool opened = decode_ahead_.open(path);
        if (opened) {
            const auto& metadata = decode_ahead_.metadata();
            published_codec_.store(
                static_cast<uint32_t>(metadata_codec(metadata)),
                std::memory_order_relaxed);
            published_source_sample_rate_hz_.store(
                metadata.sample_rate_hz, std::memory_order_relaxed);
            published_source_channel_count_.store(
                metadata.channel_count, std::memory_order_relaxed);
            published_duration_frames_.store(
                metadata.frame_count, std::memory_order_relaxed);
        } else {
            clear_published_metadata();
        }
        published_worker_state_.store(
            static_cast<uint32_t>(decode_ahead_.state()),
            std::memory_order_relaxed);
        published_decode_error_.store(
            static_cast<uint32_t>(decode_ahead_.last_error()),
            std::memory_order_relaxed);
        publish_transition(
            decode_ahead_.generation(),
            0,
            opened ? decode_ahead_.metadata().channel_count : 0);
        return opened;
    }

    [[nodiscard]] bool worker_seek(uint64_t media_frame) noexcept {
        begin_transition();
        const bool sought = decode_ahead_.seek(media_frame);
        published_decode_error_.store(
            static_cast<uint32_t>(decode_ahead_.last_error()),
            std::memory_order_relaxed);
        if (sought) {
            published_generation_.store(
                decode_ahead_.generation(), std::memory_order_relaxed);
            published_media_frame_.store(media_frame, std::memory_order_relaxed);
            published_worker_state_.store(
                static_cast<uint32_t>(decode_ahead_.state()),
                std::memory_order_relaxed);
        }
        finish_transition();
        return sought;
    }

    void worker_close() noexcept {
        begin_transition();
        decode_ahead_.close();
        clear_published_metadata();
        published_worker_state_.store(
            static_cast<uint32_t>(DecodeAheadState::Closed),
            std::memory_order_relaxed);
        published_decode_error_.store(
            static_cast<uint32_t>(DecodeError::None),
            std::memory_order_relaxed);
        publish_transition(decode_ahead_.generation(), 0, 0);
    }

    [[nodiscard]] DecodeAheadRefillResult worker_refill() noexcept {
        const auto result = decode_ahead_.refill();
        published_decode_error_.store(
            static_cast<uint32_t>(decode_ahead_.last_error()),
            std::memory_order_relaxed);
        published_worker_state_.store(
            static_cast<uint32_t>(result.state), std::memory_order_release);
        return result;
    }

    [[nodiscard]] DecodeAheadState worker_state() const noexcept {
        return decode_ahead_.state();
    }

    [[nodiscard]] bool set_rate_immediate(double rate) noexcept {
        return renderer_.set_rate_immediate(rate);
    }

    [[nodiscard]] bool ramp_rate(double rate, uint32_t render_frames) noexcept {
        return renderer_.ramp_rate(rate, render_frames);
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
        PlaybackSnapshot snapshot{};
        if (!try_read_snapshot(snapshot)) {
            return renderer_.render_suspended(
                output_sample_rate_hz,
                requested_frames,
                timestamp_ns,
                sequence_number,
                output,
                controls);
        }
        if (snapshot.generation != adopted_generation_) {
            renderer_.prepare(
                snapshot.generation,
                static_cast<double>(snapshot.media_frame),
                static_cast<uint16_t>(snapshot.channel_count));
            adopted_generation_ = snapshot.generation;
        }
        return renderer_.render(
            output_sample_rate_hz,
            requested_frames,
            timestamp_ns,
            sequence_number,
            output,
            controls);
    }

    [[nodiscard]] double media_position() const noexcept {
        return renderer_.media_position();
    }

    [[nodiscard]] double current_rate() const noexcept {
        return renderer_.current_rate();
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
        return renderer_.pitch_lock_enabled();
    }

    [[nodiscard]] uint32_t algorithmic_latency_frames() const noexcept {
        return renderer_.algorithmic_latency_frames();
    }

    [[nodiscard]] double minimum_rate() const noexcept {
        return renderer_.minimum_rate();
    }

    [[nodiscard]] double maximum_rate() const noexcept {
        return renderer_.maximum_rate();
    }

    [[nodiscard]] bool try_read_diagnostics(
        PlaybackDiagnostics& diagnostics) const noexcept {
        for (uint32_t attempt = 0; attempt < 2; ++attempt) {
            const uint64_t before = transition_version_.load(
                std::memory_order_acquire);
            if ((before & 1U) != 0) {
                return false;
            }
            diagnostics.generation = published_generation_.load(
                std::memory_order_relaxed);
            diagnostics.duration_frames = published_duration_frames_.load(
                std::memory_order_relaxed);
            diagnostics.source_sample_rate_hz = published_source_sample_rate_hz_.load(
                std::memory_order_relaxed);
            diagnostics.source_channel_count = static_cast<uint16_t>(
                published_source_channel_count_.load(std::memory_order_relaxed));
            diagnostics.codec = static_cast<MediaCodec>(
                published_codec_.load(std::memory_order_relaxed));
            diagnostics.worker_state = static_cast<DecodeAheadState>(
                published_worker_state_.load(std::memory_order_acquire));
            diagnostics.decode_error = static_cast<DecodeError>(
                published_decode_error_.load(std::memory_order_relaxed));
            diagnostics.resident_frames = decode_ahead_.pool().resident_frames();
            const auto buffered_range = decode_ahead_.pool().buffered_range(
                diagnostics.generation);
            diagnostics.buffered_start_frame = buffered_range.start_frame;
            diagnostics.buffered_end_frame = buffered_range.end_frame;
            const uint64_t after = transition_version_.load(
                std::memory_order_acquire);
            if (before == after) {
                return true;
            }
        }
        return false;
    }

private:
    struct PlaybackSnapshot {
        uint64_t generation{0};
        uint64_t media_frame{0};
        uint32_t channel_count{0};
    };

    template<typename Metadata>
    static constexpr MediaCodec metadata_codec(
        const Metadata& metadata) noexcept {
        if constexpr (requires { metadata.codec; }) {
            return static_cast<MediaCodec>(metadata.codec);
        } else if constexpr (requires { Decoder::codec; }) {
            return static_cast<MediaCodec>(Decoder::codec);
        } else {
            return MediaCodec::Unknown;
        }
    }

    void begin_transition() noexcept {
        transition_version_.fetch_add(1, std::memory_order_acq_rel);
    }

    void finish_transition() noexcept {
        transition_version_.fetch_add(1, std::memory_order_release);
    }

    void clear_published_metadata() noexcept {
        published_codec_.store(
            static_cast<uint32_t>(MediaCodec::Unknown), std::memory_order_relaxed);
        published_source_sample_rate_hz_.store(0, std::memory_order_relaxed);
        published_source_channel_count_.store(0, std::memory_order_relaxed);
        published_duration_frames_.store(0, std::memory_order_relaxed);
    }

    void publish_transition(
        uint64_t generation,
        uint64_t media_frame,
        uint32_t channel_count) noexcept {
        published_generation_.store(generation, std::memory_order_relaxed);
        published_media_frame_.store(media_frame, std::memory_order_relaxed);
        published_channel_count_.store(channel_count, std::memory_order_relaxed);
        finish_transition();
    }

    [[nodiscard]] bool try_read_snapshot(PlaybackSnapshot& snapshot) const noexcept {
        for (uint32_t attempt = 0; attempt < 2; ++attempt) {
            const uint64_t before = transition_version_.load(std::memory_order_acquire);
            if ((before & 1U) != 0) {
                return false;
            }
            snapshot.generation = published_generation_.load(std::memory_order_relaxed);
            snapshot.media_frame = published_media_frame_.load(std::memory_order_relaxed);
            snapshot.channel_count = published_channel_count_.load(std::memory_order_relaxed);
            const uint64_t after = transition_version_.load(std::memory_order_acquire);
            if (before == after) {
                return true;
            }
        }
        return false;
    }

    DecodeAhead<Decoder, ChunkCount> decode_ahead_;
    DecodeAheadRenderer<ChunkCount> renderer_;
    std::atomic<uint64_t> transition_version_{0};
    std::atomic<uint64_t> published_generation_{0};
    std::atomic<uint64_t> published_media_frame_{0};
    std::atomic<uint64_t> published_duration_frames_{0};
    std::atomic<uint32_t> published_channel_count_{0};
    std::atomic<uint32_t> published_source_sample_rate_hz_{0};
    std::atomic<uint32_t> published_source_channel_count_{0};
    std::atomic<uint32_t> published_codec_{static_cast<uint32_t>(MediaCodec::Unknown)};
    std::atomic<uint32_t> published_worker_state_{
        static_cast<uint32_t>(DecodeAheadState::Closed)};
    std::atomic<uint32_t> published_decode_error_{
        static_cast<uint32_t>(DecodeError::None)};
    uint64_t adopted_generation_{0};
};

} // namespace musicrat::backends::media