#pragma once

#include <musicrat/dsp/gain.hpp>
#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/gain.hpp>
#include <musicrat/protocol/parameter_events.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace musicrat::dsp {

class GainProcessor {
public:
    using Sample = CommRaT::Messages::AudioBlock::Sample;

    explicit GainProcessor(
        const CommRaT::Parameters::Gain& parameters = {}) noexcept {
        set_parameters_immediate(parameters);
    }

    void set_parameters_immediate(
        const CommRaT::Parameters::Gain& parameters) noexcept {
        parameters_ = normalize(parameters);
        gain_.set_immediate(effective_gain(parameters_.gain));
    }

    void set_parameters(const CommRaT::Parameters::Gain& parameters) noexcept {
        parameters_ = normalize(parameters);
        gain_.ramp_to(effective_gain(parameters_.gain), parameters_.smoothing_samples);
    }

    void process(
        const CommRaT::Messages::AudioBlock& input,
        const CommRaT::Messages::ParameterEventBlock* parameter_events,
        CommRaT::Messages::AudioBlock& output) noexcept {
        clear_audio_channels(output);
        copy_audio_metadata(input, output);

        if (validate_audio_block(input) != AudioBlockValidationError::None) {
            mark_invalid_audio_block(output);
            return;
        }

        const auto* events = events_are_valid(parameter_events, input.frame_count)
            ? parameter_events
            : nullptr;
        std::size_t event_index = 0;
        for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
            while (events != nullptr && event_index < events->events.size()
                && events->events[event_index].sample_offset == frame) {
                apply_event(events->events[event_index]);
                ++event_index;
            }

            const auto frame_gain = gain_.next_gain();
            for (uint16_t channel = 0; channel < input.channel_count; ++channel) {
                output.channels[channel].push_back(
                    input.channels[channel][frame] * frame_gain);
            }
        }
    }

private:
    static CommRaT::Parameters::Gain normalize(
        CommRaT::Parameters::Gain parameters) noexcept {
        if (!std::isfinite(parameters.gain)) {
            parameters.gain = 1.0;
        }
        parameters.smoothing_samples = std::min(
            parameters.smoothing_samples,
            static_cast<uint32_t>(CommRaT::Messages::AudioBlock::MAX_FRAMES));
        return parameters;
    }

    static bool events_are_valid(
        const CommRaT::Messages::ParameterEventBlock* block,
        uint32_t frame_count) noexcept {
        if (block == nullptr) {
            return false;
        }
        uint32_t previous_offset = 0;
        for (std::size_t index = 0; index < block->events.size(); ++index) {
            const auto offset = block->events[index].sample_offset;
            if (offset >= frame_count || (index > 0 && offset < previous_offset)) {
                return false;
            }
            previous_offset = offset;
        }
        return true;
    }

    void apply_event(const CommRaT::Messages::ParameterEvent& event) noexcept {
        if (event.parameter_id != CommRaT::Parameters::GAIN_PARAMETER_ID
            || !std::isfinite(event.value)) {
            return;
        }
        gain_.ramp_to(
            effective_gain(static_cast<Sample>(event.value)),
            parameters_.smoothing_samples);
    }

    [[nodiscard]] Sample effective_gain(Sample gain) const noexcept {
        if (parameters_.muted) {
            return 0.0;
        }
        return parameters_.invert_polarity ? -gain : gain;
    }

    CommRaT::Parameters::Gain parameters_{};
    musicrat::dsp::Gain gain_{};
};

} // namespace musicrat::dsp