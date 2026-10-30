#pragma once

#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/dc_blocker.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace musicrat::dsp {

class DcBlockerProcessor {
public:
    using Sample = CommRaT::Messages::AudioBlock::Sample;

    explicit DcBlockerProcessor(
        const CommRaT::Parameters::DcBlocker& parameters = {}) noexcept {
        set_parameters(parameters);
    }

    void set_parameters(CommRaT::Parameters::DcBlocker parameters) noexcept {
        if (!std::isfinite(parameters.cutoff_hz)) {
            parameters.cutoff_hz = 20.0;
        }
        parameters.cutoff_hz = std::max(parameters.cutoff_hz, 0.1);
        parameters_ = parameters;
    }

    void process(
        const CommRaT::Messages::AudioBlock& input,
        CommRaT::Messages::AudioBlock& output) noexcept {
        clear_audio_channels(output);
        copy_audio_metadata(input, output);
        if (validate_audio_block(input) != AudioBlockValidationError::None) {
            mark_invalid_audio_block(output);
            return;
        }

        if ((input.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY) != 0) {
            reset();
        }
        const double cutoff = std::min(
            parameters_.cutoff_hz, input.sample_rate_hz * 0.49);
        const double pole = std::exp(
            -2.0 * std::numbers::pi * cutoff / input.sample_rate_hz);

        for (uint16_t channel = 0; channel < input.channel_count; ++channel) {
            for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
                const double sample = input.channels[channel][frame];
                const double filtered = sample - previous_input_[channel]
                    + pole * previous_output_[channel];
                previous_input_[channel] = sample;
                previous_output_[channel] = filtered;
                output.channels[channel].push_back(
                    static_cast<Sample>(filtered));
            }
        }
    }

    void reset() noexcept {
        previous_input_.fill(0.0);
        previous_output_.fill(0.0);
    }

private:
    CommRaT::Parameters::DcBlocker parameters_{};
    std::array<double, CommRaT::Messages::AudioBlock::MAX_CHANNELS>
        previous_input_{};
    std::array<double, CommRaT::Messages::AudioBlock::MAX_CHANNELS>
        previous_output_{};
};

} // namespace musicrat::dsp
