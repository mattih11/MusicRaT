#pragma once

#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/deck_control.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace musicrat::dsp {

struct DecodedAudioChunkView {
    const CommRaT::Messages::AudioBlock* audio{nullptr};
    uint64_t start_frame{0};
    uint64_t generation{0};
};

struct VarispeedRenderResult {
    uint32_t source_frames_rendered{0};
    uint32_t silent_frames{0};
    double media_position{0.0};
    bool reached_end{false};
    bool generation_mismatch{false};
};

class VarispeedRenderer {
public:
    using AudioBlock = CommRaT::Messages::AudioBlock;
    using Sample = AudioBlock::Sample;

    static constexpr double minimum_rate = 0.25;
    static constexpr double maximum_rate = 4.0;

    [[nodiscard]] bool set_rate_bounds(double minimum, double maximum) noexcept {
        if (!std::isfinite(minimum) || !std::isfinite(maximum)
            || minimum < minimum_rate || maximum > maximum_rate
            || minimum > maximum || current_rate_ < minimum
            || current_rate_ > maximum) {
            return false;
        }
        active_minimum_rate_ = minimum;
        active_maximum_rate_ = maximum;
        return true;
    }

    void prepare(uint64_t generation, double media_position = 0.0) noexcept {
        generation_ = generation;
        media_position_ = std::max(0.0, media_position);
        pending_discontinuity_ = true;
        loop_enabled_ = false;
    }

    [[nodiscard]] bool set_rate_immediate(double rate) noexcept {
        if (!valid_rate(rate)) {
            return false;
        }
        current_rate_ = rate;
        target_rate_ = rate;
        rate_increment_ = 0.0;
        rate_ramp_frames_ = 0;
        return true;
    }

    [[nodiscard]] bool ramp_rate(double rate, uint32_t render_frames) noexcept {
        if (!valid_rate(rate)) {
            return false;
        }
        target_rate_ = rate;
        rate_ramp_frames_ = render_frames;
        if (render_frames == 0) {
            return set_rate_immediate(rate);
        }
        rate_increment_ = (target_rate_ - current_rate_)
            / static_cast<double>(render_frames);
        return true;
    }

    void set_playing(bool playing) noexcept {
        playing_ = playing;
    }

    [[nodiscard]] bool playing() const noexcept {
        return playing_;
    }

    [[nodiscard]] double current_rate() const noexcept {
        return current_rate_;
    }

    [[nodiscard]] double media_position() const noexcept {
        return media_position_;
    }

    [[nodiscard]] double retention_floor() const noexcept {
        return loop_enabled_
            ? std::min(media_position_, static_cast<double>(loop_start_frame_))
            : media_position_;
    }

    [[nodiscard]] bool loop_enabled() const noexcept {
        return loop_enabled_;
    }

    [[nodiscard]] uint64_t generation() const noexcept {
        return generation_;
    }

    VarispeedRenderResult render(
        const DecodedAudioChunkView& chunk,
        double output_sample_rate_hz,
        uint32_t requested_frames,
        uint64_t timestamp_ns,
        uint64_t sequence_number,
        AudioBlock& output,
        const CommRaT::Messages::DeckControlEventBlock* controls = nullptr,
        std::span<double> rendered_rates = {}) noexcept {
        return render(
            std::span<const DecodedAudioChunkView>{&chunk, 1},
            output_sample_rate_hz,
            requested_frames,
            timestamp_ns,
            sequence_number,
            output,
            controls,
            rendered_rates);
    }

    VarispeedRenderResult render(
        std::span<const DecodedAudioChunkView> chunks,
        double output_sample_rate_hz,
        uint32_t requested_frames,
        uint64_t timestamp_ns,
        uint64_t sequence_number,
        AudioBlock& output,
        const CommRaT::Messages::DeckControlEventBlock* controls = nullptr,
        std::span<double> rendered_rates = {}) noexcept {
        initialize_output(
            chunks.empty() ? nullptr : chunks.front().audio,
            output_sample_rate_hz,
            requested_frames,
            timestamp_ns,
            sequence_number,
            output);

        VarispeedRenderResult result{.media_position = media_position_};
        if (!valid_request(chunks, output_sample_rate_hz, requested_frames)
            || (!rendered_rates.empty()
                && rendered_rates.size() < requested_frames)) {
            mark_invalid_audio_block(output);
            return result;
        }

        for (const auto& chunk : chunks) {
            if (chunk.generation != generation_) {
                result.generation_mismatch = true;
                fill_silence(output, requested_frames);
                fill_rates(rendered_rates, requested_frames, current_rate_);
                output.flags |= CommRaT::Messages::AUDIO_BLOCK_SILENCE
                    | CommRaT::Messages::AUDIO_BLOCK_UNDERRUN;
                result.silent_frames = requested_frames;
                return result;
            }
        }

        if (pending_discontinuity_) {
            output.flags |= CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
            pending_discontinuity_ = false;
        }

        const double final_end = find_final_end(chunks);
        const auto* valid_controls = controls_are_valid(controls, requested_frames)
            ? controls
            : nullptr;
        std::size_t control_index = 0;

        for (uint32_t frame = 0; frame < requested_frames; ++frame) {
            while (valid_controls != nullptr
                && control_index < valid_controls->events.size()
                && valid_controls->events[control_index].sample_offset == frame) {
                apply_control(valid_controls->events[control_index], chunks, output);
                ++control_index;
            }
            wrap_loop_position(output);
            if (!playing_) {
                append_silent_frame(output);
                publish_rate(rendered_rates, frame, current_rate_);
                ++result.silent_frames;
                continue;
            }
            if (!sample_frame(chunks, output)) {
                append_silent_frame(output);
                publish_rate(rendered_rates, frame, current_rate_);
                ++result.silent_frames;
                if (media_position_ >= final_end) {
                    result.reached_end = true;
                    output.flags |= CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
                } else {
                    output.flags |= CommRaT::Messages::AUDIO_BLOCK_UNDERRUN;
                }
                continue;
            }

            ++result.source_frames_rendered;
            const double rendered_rate = next_rate();
            publish_rate(rendered_rates, frame, rendered_rate);
            const double source_advance = rendered_rate
                * chunks.front().audio->sample_rate_hz / output_sample_rate_hz;
            media_position_ += source_advance;
            wrap_loop_position(output);
        }

        if (result.source_frames_rendered == 0) {
            output.flags |= CommRaT::Messages::AUDIO_BLOCK_SILENCE;
        }
        result.media_position = media_position_;
        return result;
    }

    VarispeedRenderResult render_underrun(
        uint16_t channel_count,
        double output_sample_rate_hz,
        uint32_t requested_frames,
        uint64_t timestamp_ns,
        uint64_t sequence_number,
        AudioBlock& output,
        const CommRaT::Messages::DeckControlEventBlock* controls = nullptr) noexcept {
        initialize_output(
            nullptr,
            output_sample_rate_hz,
            requested_frames,
            timestamp_ns,
            sequence_number,
            output);
        output.channel_count = channel_count;

        VarispeedRenderResult result{.media_position = media_position_};
        if (!std::isfinite(output_sample_rate_hz)
            || output_sample_rate_hz <= 0.0
            || channel_count > AudioBlock::MAX_CHANNELS
            || requested_frames == 0
            || requested_frames > AudioBlock::MAX_FRAMES) {
            mark_invalid_audio_block(output);
            return result;
        }

        const auto* valid_controls = controls_are_valid(controls, requested_frames)
            ? controls
            : nullptr;
        std::size_t control_index = 0;
        for (uint32_t frame = 0; frame < requested_frames; ++frame) {
            while (valid_controls != nullptr
                && control_index < valid_controls->events.size()
                && valid_controls->events[control_index].sample_offset == frame) {
                apply_control(valid_controls->events[control_index], {}, output);
                ++control_index;
            }
            append_silent_frame(output);
        }
        output.flags |= CommRaT::Messages::AUDIO_BLOCK_SILENCE
            | CommRaT::Messages::AUDIO_BLOCK_UNDERRUN;
        result.silent_frames = requested_frames;
        return result;
    }

private:
    bool valid_rate(double rate) const noexcept {
        return std::isfinite(rate)
            && rate >= active_minimum_rate_
            && rate <= active_maximum_rate_;
    }

    static bool controls_are_valid(
        const CommRaT::Messages::DeckControlEventBlock* controls,
        uint32_t frame_count) noexcept {
        if (controls == nullptr) {
            return false;
        }
        uint32_t previous_offset = 0;
        for (std::size_t index = 0; index < controls->events.size(); ++index) {
            const auto offset = controls->events[index].sample_offset;
            if (offset >= frame_count || (index > 0 && offset < previous_offset)) {
                return false;
            }
            previous_offset = offset;
        }
        return true;
    }

    void apply_control(
        const CommRaT::Messages::DeckControlEvent& control,
        std::span<const DecodedAudioChunkView> chunks,
        AudioBlock& output) noexcept {
        switch (control.type) {
        case CommRaT::Messages::DECK_CONTROL_PLAY:
            set_playing(true);
            break;
        case CommRaT::Messages::DECK_CONTROL_PAUSE:
            set_playing(false);
            break;
        case CommRaT::Messages::DECK_CONTROL_SET_RATE:
            static_cast<void>(set_rate_immediate(control.value));
            break;
        case CommRaT::Messages::DECK_CONTROL_RAMP_RATE:
            static_cast<void>(ramp_rate(control.value, control.ramp_frames));
            break;
        case CommRaT::Messages::DECK_CONTROL_SET_CUE:
            if (valid_frame_position(control.value)) {
                cue_frame_ = static_cast<uint64_t>(control.value);
                cue_set_ = true;
            }
            break;
        case CommRaT::Messages::DECK_CONTROL_RETURN_TO_CUE:
            if (cue_set_ && find_chunk(chunks, cue_frame_) != nullptr) {
                media_position_ = static_cast<double>(cue_frame_);
                output.flags |= CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
            }
            break;
        case CommRaT::Messages::DECK_CONTROL_CLEAR_CUE:
            cue_set_ = false;
            break;
        case CommRaT::Messages::DECK_CONTROL_SET_LOOP_START:
            if (valid_frame_position(control.value)) {
                loop_start_frame_ = static_cast<uint64_t>(control.value);
                loop_enabled_ = false;
            }
            break;
        case CommRaT::Messages::DECK_CONTROL_SET_LOOP_END:
            if (valid_frame_position(control.value)) {
                loop_end_frame_ = static_cast<uint64_t>(control.value);
                loop_enabled_ = false;
            }
            break;
        case CommRaT::Messages::DECK_CONTROL_ENABLE_LOOP:
            loop_enabled_ = loop_start_frame_ < loop_end_frame_
                && range_is_resident(loop_start_frame_, loop_end_frame_, chunks);
            break;
        case CommRaT::Messages::DECK_CONTROL_DISABLE_LOOP:
            loop_enabled_ = false;
            break;
        default:
            break;
        }
    }

    static bool valid_frame_position(double value) noexcept {
        constexpr double maximum_exact_frame = 9007199254740991.0;
        return std::isfinite(value) && value >= 0.0
            && value <= maximum_exact_frame
            && std::trunc(value) == value;
    }

    static bool range_is_resident(
        uint64_t start,
        uint64_t end,
        std::span<const DecodedAudioChunkView> chunks) noexcept {
        uint64_t cursor = start;
        while (cursor < end) {
            const auto* chunk = find_chunk(chunks, cursor);
            if (chunk == nullptr) {
                return false;
            }
            const uint64_t chunk_end = chunk->start_frame
                + chunk->audio->frame_count;
            if (chunk_end <= cursor) {
                return false;
            }
            cursor = std::min(chunk_end, end);
        }
        return true;
    }

    void wrap_loop_position(AudioBlock& output) noexcept {
        if (!loop_enabled_
            || media_position_ < static_cast<double>(loop_end_frame_)) {
            return;
        }
        const double loop_start = static_cast<double>(loop_start_frame_);
        const double loop_length = static_cast<double>(
            loop_end_frame_ - loop_start_frame_);
        media_position_ = loop_start
            + std::fmod(media_position_ - loop_start, loop_length);
        output.flags |= CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
    }

    static bool valid_request(
        std::span<const DecodedAudioChunkView> chunks,
        double output_sample_rate_hz,
        uint32_t requested_frames) noexcept {
        if (chunks.empty() || chunks.front().audio == nullptr
            || !std::isfinite(output_sample_rate_hz)
            || output_sample_rate_hz <= 0.0
            || requested_frames == 0
            || requested_frames > AudioBlock::MAX_FRAMES) {
            return false;
        }
        const auto channel_count = chunks.front().audio->channel_count;
        const auto sample_rate_hz = chunks.front().audio->sample_rate_hz;
        for (const auto& chunk : chunks) {
            if (chunk.audio == nullptr
                || validate_audio_block(*chunk.audio) != AudioBlockValidationError::None
                || chunk.audio->channel_count != channel_count
                || chunk.audio->sample_rate_hz != sample_rate_hz) {
                return false;
            }
        }
        return true;
    }

    static void initialize_output(
        const AudioBlock* source,
        double sample_rate_hz,
        uint32_t frame_count,
        uint64_t timestamp_ns,
        uint64_t sequence_number,
        AudioBlock& output) noexcept {
        clear_audio_channels(output);
        output.sample_rate_hz = sample_rate_hz;
        output.timestamp_ns = timestamp_ns;
        output.sequence_number = sequence_number;
        output.frame_count = frame_count;
        output.channel_count = source == nullptr ? 0 : source->channel_count;
        output.flags = 0;
    }

    bool sample_frame(
        std::span<const DecodedAudioChunkView> chunks,
        AudioBlock& output) const noexcept {
        const auto first_frame = static_cast<uint64_t>(media_position_);
        const auto* first_chunk = find_chunk(chunks, first_frame);
        if (first_chunk == nullptr) {
            return false;
        }

        const double fraction = media_position_ - static_cast<double>(first_frame);
        const auto first_end = first_chunk->start_frame
            + first_chunk->audio->frame_count;
        const bool clamp_final_sample = fraction != 0.0
            && first_frame + 1 == first_end
            && (first_chunk->audio->flags
                & CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM) != 0;
        uint64_t second_frame = first_frame + 1;
        if (loop_enabled_ && second_frame >= loop_end_frame_) {
            second_frame = loop_start_frame_;
        }
        const auto* second_chunk = fraction == 0.0 || clamp_final_sample
            ? first_chunk
            : find_chunk(chunks, second_frame);
        if (second_chunk == nullptr) {
            return false;
        }

        const auto first_index = static_cast<uint32_t>(
            first_frame - first_chunk->start_frame);
        const auto second_index = fraction == 0.0 || clamp_final_sample
            ? first_index
            : static_cast<uint32_t>(second_frame - second_chunk->start_frame);
        for (uint16_t channel = 0;
             channel < first_chunk->audio->channel_count;
             ++channel) {
            const double first_sample = first_chunk->audio->channels[channel][first_index];
            const double second_sample = second_chunk->audio->channels[channel][second_index];
            output.channels[channel].push_back(static_cast<Sample>(
                first_sample + (second_sample - first_sample) * fraction));
        }
        return true;
    }

    static const DecodedAudioChunkView* find_chunk(
        std::span<const DecodedAudioChunkView> chunks,
        uint64_t frame) noexcept {
        for (const auto& chunk : chunks) {
            const uint64_t end = chunk.start_frame + chunk.audio->frame_count;
            if (frame >= chunk.start_frame && frame < end) {
                return &chunk;
            }
        }
        return nullptr;
    }

    static double find_final_end(
        std::span<const DecodedAudioChunkView> chunks) noexcept {
        for (const auto& chunk : chunks) {
            if ((chunk.audio->flags
                    & CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM) != 0) {
                return static_cast<double>(chunk.start_frame)
                    + chunk.audio->frame_count;
            }
        }
        return std::numeric_limits<double>::infinity();
    }

    static void append_silent_frame(AudioBlock& output) noexcept {
        for (uint16_t channel = 0; channel < output.channel_count; ++channel) {
            output.channels[channel].push_back(Sample{0});
        }
    }

    static void fill_silence(AudioBlock& output, uint32_t frame_count) noexcept {
        for (uint32_t frame = 0; frame < frame_count; ++frame) {
            append_silent_frame(output);
        }
    }

    static void publish_rate(
        std::span<double> rendered_rates,
        uint32_t frame,
        double rate) noexcept {
        if (!rendered_rates.empty()) {
            rendered_rates[frame] = rate;
        }
    }

    static void fill_rates(
        std::span<double> rendered_rates,
        uint32_t frame_count,
        double rate) noexcept {
        if (!rendered_rates.empty()) {
            std::fill_n(rendered_rates.begin(), frame_count, rate);
        }
    }

    double next_rate() noexcept {
        if (rate_ramp_frames_ > 0) {
            current_rate_ += rate_increment_;
            --rate_ramp_frames_;
            if (rate_ramp_frames_ == 0) {
                current_rate_ = target_rate_;
            }
        }
        return current_rate_;
    }

    uint64_t generation_{0};
    double media_position_{0.0};
    double current_rate_{1.0};
    double target_rate_{1.0};
    double rate_increment_{0.0};
    double active_minimum_rate_{minimum_rate};
    double active_maximum_rate_{maximum_rate};
    uint32_t rate_ramp_frames_{0};
    uint64_t cue_frame_{0};
    uint64_t loop_start_frame_{0};
    uint64_t loop_end_frame_{0};
    bool playing_{true};
    bool pending_discontinuity_{true};
    bool cue_set_{false};
    bool loop_enabled_{false};
};

} // namespace musicrat::dsp