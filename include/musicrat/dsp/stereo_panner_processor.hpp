#pragma once

#include <musicrat/dsp/gain.hpp>
#include <musicrat/dsp/stereo_panner.hpp>
#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/parameter_events.hpp>
#include <musicrat/protocol/stereo_panner.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace musicrat::dsp {

class StereoPannerProcessor {
public:
    using Sample = CommRaT::Messages::AudioBlock::Sample;

    static_assert(CommRaT::Messages::AudioBlock::MAX_CHANNELS >= 2);

    explicit StereoPannerProcessor(
        const CommRaT::Parameters::StereoPanner& parameters = {}) noexcept {
        set_parameters_immediate(parameters);
    }

    void set_parameters_immediate(
        const CommRaT::Parameters::StereoPanner& parameters) noexcept {
        parameters_ = normalize(parameters);
        const auto gains = stereo_pan_gains(parameters_.pan, parameters_.pan_law);
        left_gain_.set_immediate(gains.left);
        right_gain_.set_immediate(gains.right);
    }

    void set_parameters(
        const CommRaT::Parameters::StereoPanner& parameters) noexcept {
        parameters_ = normalize(parameters);
        ramp_to(parameters_.pan);
    }

    void process(
        const CommRaT::Messages::AudioBlock& input,
        const CommRaT::Messages::ParameterEventBlock* parameter_events,
        CommRaT::Messages::AudioBlock& output) noexcept {
        clear_audio_channels(output);
        copy_audio_metadata(input, output);

        if (validate_audio_block(input) != AudioBlockValidationError::None
            || input.channel_count != 1) {
            mark_invalid_audio_block(output);
            return;
        }

        output.channel_count = 2;
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

            const auto sample = input.channels[0][frame];
            output.channels[0].push_back(sample * left_gain_.next_gain());
            output.channels[1].push_back(sample * right_gain_.next_gain());
        }
    }

private:
    static CommRaT::Parameters::StereoPanner normalize(
        CommRaT::Parameters::StereoPanner parameters) noexcept {
        if (!std::isfinite(parameters.pan)) {
            parameters.pan = 0.0;
        }
        parameters.pan = std::clamp(parameters.pan, Sample{-1}, Sample{1});
        if (parameters.pan_law != CommRaT::Parameters::PAN_LAW_LINEAR
            && parameters.pan_law != CommRaT::Parameters::PAN_LAW_EQUAL_POWER) {
            parameters.pan_law = CommRaT::Parameters::PAN_LAW_EQUAL_POWER;
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
        if (event.parameter_id != CommRaT::Parameters::PAN_PARAMETER_ID
            || !std::isfinite(event.value)) {
            return;
        }
        ramp_to(static_cast<Sample>(std::clamp(event.value, -1.0, 1.0)));
    }

    void ramp_to(Sample pan) noexcept {
        const auto gains = stereo_pan_gains(pan, parameters_.pan_law);
        left_gain_.ramp_to(gains.left, parameters_.smoothing_samples);
        right_gain_.ramp_to(gains.right, parameters_.smoothing_samples);
    }

    CommRaT::Parameters::StereoPanner parameters_{};
    musicrat::dsp::Gain left_gain_{};
    musicrat::dsp::Gain right_gain_{};
};

} // namespace musicrat::dsp