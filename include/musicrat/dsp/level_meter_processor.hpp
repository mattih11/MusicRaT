#pragma once

#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/telemetry.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace musicrat::dsp {

class LevelMeterProcessor {
public:
    using Sample = CommRaT::Messages::AudioBlock::Sample;

    explicit LevelMeterProcessor(
        const CommRaT::Parameters::LevelMeter& parameters = {}) noexcept {
        set_parameters(parameters);
    }

    void set_parameters(const CommRaT::Parameters::LevelMeter& parameters) noexcept {
        clipping_threshold_ = std::isfinite(parameters.clipping_threshold)
                && parameters.clipping_threshold > Sample{0}
            ? parameters.clipping_threshold
            : Sample{1};
    }

    void process(
        const CommRaT::Messages::AudioBlock& input,
        CommRaT::Messages::AudioBlock& output,
        CommRaT::Messages::LevelMeterBlock& meter) const noexcept {
        clear_audio_channels(output);
        copy_audio_metadata(input, output);
        clear_meter(meter);
        meter.timestamp_ns = input.timestamp_ns;
        meter.sequence_number = input.sequence_number;

        if (validate_audio_block(input) != AudioBlockValidationError::None) {
            mark_invalid_audio_block(output);
            meter.flags = CommRaT::Messages::LEVEL_METER_INVALID;
            return;
        }

        meter.channel_count = input.channel_count;
        for (uint16_t channel = 0; channel < input.channel_count; ++channel) {
            double sum_squares = 0.0;
            Sample peak = 0.0;
            bool valid_samples = true;

            for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
                const Sample sample = input.channels[channel][frame];
                output.channels[channel].push_back(sample);
                if (!std::isfinite(sample)) {
                    valid_samples = false;
                    continue;
                }
                const Sample magnitude = std::abs(sample);
                peak = std::max(peak, magnitude);
                sum_squares += static_cast<double>(sample)
                    * static_cast<double>(sample);
            }

            if (!valid_samples) {
                mark_invalid_audio_block(output);
                clear_meter(meter);
                meter.timestamp_ns = input.timestamp_ns;
                meter.sequence_number = input.sequence_number;
                meter.flags = CommRaT::Messages::LEVEL_METER_INVALID;
                return;
            }

            meter.peak[channel] = peak;
            meter.rms[channel] = input.frame_count == 0
                ? Sample{0}
                : static_cast<Sample>(std::sqrt(
                    sum_squares / static_cast<double>(input.frame_count)));
            meter.clipped[channel] = peak >= clipping_threshold_ ? 1U : 0U;
        }
    }

private:
    static void clear_meter(CommRaT::Messages::LevelMeterBlock& meter) noexcept {
        meter.peak.fill(0.0);
        meter.rms.fill(0.0);
        meter.clipped.fill(0);
        meter.channel_count = 0;
        meter.flags = 0;
    }

    Sample clipping_threshold_{1.0};
};

} // namespace musicrat::dsp