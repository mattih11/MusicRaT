#pragma once

#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace musicrat::dsp {

class PitchLockProcessor {
public:
    using AudioBlock = CommRaT::Messages::AudioBlock;
    using Sample = AudioBlock::Sample;

    static constexpr double minimum_rate = 0.5;
    static constexpr double maximum_rate = 2.0;

    explicit PitchLockProcessor(uint32_t sample_rate_hz)
        : sample_rate_hz_(validate_sample_rate(sample_rate_hz))
        , stretcher_(
            sample_rate_hz_,
            AudioBlock::MAX_CHANNELS,
            RubberBand::RubberBandStretcher::OptionProcessRealTime
                | RubberBand::RubberBandStretcher::OptionThreadingNever
                | RubberBand::RubberBandStretcher::OptionWindowShort
                | RubberBand::RubberBandStretcher::OptionPitchHighConsistency,
            1.0,
            1.0) {
        if (AudioBlock::MAX_FRAMES > stretcher_.getProcessSizeLimit()) {
            throw std::invalid_argument(
                "MusicRaT audio block capacity exceeds Rubber Band process limit");
        }
        stretcher_.setMaxProcessSize(AudioBlock::MAX_FRAMES);
        static_cast<void>(reset(1.0));
    }

    PitchLockProcessor(const PitchLockProcessor&) = delete;
    PitchLockProcessor& operator=(const PitchLockProcessor&) = delete;

    [[nodiscard]] bool set_rate(double rate) noexcept {
        if (!valid_rate(rate)) {
            return false;
        }
        if (rate != rate_) {
            rate_ = rate;
            stretcher_.setPitchScale(1.0 / rate_);
        }
        return true;
    }

    [[nodiscard]] bool reset(double rate) noexcept {
        if (!valid_rate(rate)) {
            return false;
        }
        stretcher_.reset();
        stretcher_.setTimeRatio(1.0);
        stretcher_.setPitchScale(1.0 / rate);
        rate_ = rate;
        latency_frames_ = stretcher_.getStartDelay();
        discard_frames_ = latency_frames_;
        prime_start_padding(stretcher_.getPreferredStartPad());
        return true;
    }

    [[nodiscard]] uint32_t latency_frames() const noexcept {
        return latency_frames_ > std::numeric_limits<uint32_t>::max()
            ? std::numeric_limits<uint32_t>::max()
            : static_cast<uint32_t>(latency_frames_);
    }

    [[nodiscard]] double rate() const noexcept {
        return rate_;
    }

    [[nodiscard]] bool process(
        const AudioBlock& input,
        double rate,
        AudioBlock& output) noexcept {
        if (!valid_input(input) || !valid_rate(rate)) {
            mark_invalid(input, output);
            return false;
        }
        prepare_input(input);
        static_cast<void>(set_rate(rate));
        process_segment(0, input.frame_count);
        retrieve_output(input, output);
        return true;
    }

    [[nodiscard]] bool process(
        const AudioBlock& input,
        std::span<const double> rendered_rates,
        AudioBlock& output) noexcept {
        if (!valid_input(input) || rendered_rates.size() < input.frame_count) {
            mark_invalid(input, output);
            return false;
        }
        for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
            if (!valid_rate(rendered_rates[frame])) {
                mark_invalid(input, output);
                return false;
            }
        }

        prepare_input(input);
        uint32_t segment_start = 0;
        while (segment_start < input.frame_count) {
            const double segment_rate = rendered_rates[segment_start];
            uint32_t segment_end = segment_start + 1;
            while (segment_end < input.frame_count
                && rendered_rates[segment_end] == segment_rate) {
                ++segment_end;
            }
            static_cast<void>(set_rate(segment_rate));
            process_segment(segment_start, segment_end - segment_start);
            segment_start = segment_end;
        }
        retrieve_output(input, output);
        return true;
    }

private:
    static uint32_t validate_sample_rate(uint32_t sample_rate_hz) {
        if (sample_rate_hz < 8000 || sample_rate_hz > 192000) {
            throw std::invalid_argument(
                "Rubber Band sample rate must be between 8000 and 192000 Hz");
        }
        return sample_rate_hz;
    }

    static bool valid_rate(double rate) noexcept {
        return std::isfinite(rate)
            && rate >= minimum_rate
            && rate <= maximum_rate;
    }

    [[nodiscard]] bool valid_input(const AudioBlock& input) const noexcept {
        return validate_audio_block(input) == AudioBlockValidationError::None
            && input.sample_rate_hz == static_cast<double>(sample_rate_hz_)
            && input.frame_count > 0;
    }

    void prepare_input(const AudioBlock& input) noexcept {
        for (uint16_t channel = 0; channel < AudioBlock::MAX_CHANNELS; ++channel) {
            for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
                input_scratch_[channel][frame] = channel < input.channel_count
                    ? static_cast<float>(input.channels[channel][frame])
                    : 0.0F;
            }
        }
    }

    void process_segment(uint32_t start_frame, uint32_t frame_count) noexcept {
        for (uint16_t channel = 0; channel < AudioBlock::MAX_CHANNELS; ++channel) {
            input_channels_[channel] = input_scratch_[channel].data() + start_frame;
        }
        stretcher_.process(input_channels_.data(), frame_count, false);
    }

    void retrieve_output(const AudioBlock& input, AudioBlock& output) noexcept {
        discard_available_output();
        for (uint16_t channel = 0; channel < AudioBlock::MAX_CHANNELS; ++channel) {
            output_channels_[channel] = output_scratch_[channel].data();
        }
        const int available = stretcher_.available();
        const auto wanted = available > 0
            ? std::min<std::size_t>(
                static_cast<std::size_t>(available), input.frame_count)
            : std::size_t{0};
        const std::size_t retrieved = wanted == 0
            ? 0
            : stretcher_.retrieve(output_channels_.data(), wanted);

        clear_output(input, output);
        for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
            for (uint16_t channel = 0; channel < input.channel_count; ++channel) {
                const Sample sample = frame < retrieved
                    ? static_cast<Sample>(output_scratch_[channel][frame])
                    : Sample{0};
                output.channels[channel].push_back(sample);
            }
        }
        if (retrieved < input.frame_count) {
            output.flags |= CommRaT::Messages::AUDIO_BLOCK_UNDERRUN;
            if (retrieved == 0) {
                output.flags |= CommRaT::Messages::AUDIO_BLOCK_SILENCE;
            }
        }
    }

    static void mark_invalid(const AudioBlock& input, AudioBlock& output) noexcept {
        clear_output(input, output);
        output.flags |= CommRaT::Messages::AUDIO_BLOCK_INVALID;
    }

    void prime_start_padding(std::size_t frames) noexcept {
        for (auto& channel : input_channels_) {
            channel = zero_padding_.data();
        }
        while (frames > 0) {
            const std::size_t count = std::min<std::size_t>(
                frames, AudioBlock::MAX_FRAMES);
            stretcher_.process(input_channels_.data(), count, false);
            frames -= count;
        }
        discard_available_output();
    }

    void discard_available_output() noexcept {
        while (discard_frames_ > 0) {
            const int available = stretcher_.available();
            if (available <= 0) {
                break;
            }
            const std::size_t count = std::min<std::size_t>({
                discard_frames_,
                static_cast<std::size_t>(available),
                AudioBlock::MAX_FRAMES,
            });
            for (std::size_t channel = 0; channel < output_channels_.size(); ++channel) {
                output_channels_[channel] = output_scratch_[channel].data();
            }
            const std::size_t discarded = stretcher_.retrieve(
                output_channels_.data(), count);
            discard_frames_ -= discarded;
            if (discarded == 0) {
                break;
            }
        }
    }

    static void clear_output(const AudioBlock& input, AudioBlock& output) noexcept {
        const double sample_rate_hz = input.sample_rate_hz;
        const uint64_t timestamp_ns = input.timestamp_ns;
        const uint64_t sequence_number = input.sequence_number;
        const uint32_t frame_count = input.frame_count;
        const uint16_t channel_count = input.channel_count;
        const uint16_t flags = input.flags;
        clear_audio_channels(output);
        output.sample_rate_hz = sample_rate_hz;
        output.timestamp_ns = timestamp_ns;
        output.sequence_number = sequence_number;
        output.frame_count = frame_count;
        output.channel_count = channel_count;
        output.flags = flags;
    }

    uint32_t sample_rate_hz_;
    RubberBand::RubberBandStretcher stretcher_;
    std::array<std::array<float, AudioBlock::MAX_FRAMES>, AudioBlock::MAX_CHANNELS>
        input_scratch_{};
    std::array<std::array<float, AudioBlock::MAX_FRAMES>, AudioBlock::MAX_CHANNELS>
        output_scratch_{};
    std::array<float, AudioBlock::MAX_FRAMES> zero_padding_{};
    std::array<const float*, AudioBlock::MAX_CHANNELS> input_channels_{};
    std::array<float*, AudioBlock::MAX_CHANNELS> output_channels_{};
    std::size_t latency_frames_{0};
    std::size_t discard_frames_{0};
    double rate_{1.0};
};

} // namespace musicrat::dsp