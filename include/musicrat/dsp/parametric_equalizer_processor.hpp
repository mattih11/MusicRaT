#pragma once

#include <musicrat/dsp/biquad.hpp>
#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/parameter_events.hpp>
#include <musicrat/protocol/parametric_equalizer.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace musicrat::dsp {

class ParametricEqualizerProcessor {
public:
    using Sample = CommRaT::Messages::AudioBlock::Sample;

    explicit ParametricEqualizerProcessor(
        const CommRaT::Parameters::ParametricEqualizer& parameters = {}) noexcept {
        set_parameters(parameters);
    }

    void set_parameters(
        CommRaT::Parameters::ParametricEqualizer parameters) noexcept {
        parameters.low_shelf_frequency_hz =
            finite_or(parameters.low_shelf_frequency_hz, 120.0);
        parameters.low_shelf_gain_db =
            finite_or(parameters.low_shelf_gain_db, 0.0);
        parameters.mid_frequency_hz =
            finite_or(parameters.mid_frequency_hz, 1000.0);
        parameters.mid_gain_db = finite_or(parameters.mid_gain_db, 0.0);
        parameters.mid_q = finite_or(parameters.mid_q, 0.7071067811865476);
        parameters.high_shelf_frequency_hz =
            finite_or(parameters.high_shelf_frequency_hz, 8000.0);
        parameters.high_shelf_gain_db =
            finite_or(parameters.high_shelf_gain_db, 0.0);
        parameters.low_shelf_frequency_hz =
            std::max(parameters.low_shelf_frequency_hz, 0.1);
        parameters.low_shelf_gain_db =
            std::clamp(parameters.low_shelf_gain_db, -24.0, 24.0);
        parameters.mid_frequency_hz =
            std::max(parameters.mid_frequency_hz, 0.1);
        parameters.mid_gain_db =
            std::clamp(parameters.mid_gain_db, -24.0, 24.0);
        parameters.mid_q = std::clamp(parameters.mid_q, 0.1, 20.0);
        parameters.high_shelf_frequency_hz =
            std::max(parameters.high_shelf_frequency_hz, 0.1);
        parameters.high_shelf_gain_db =
            std::clamp(parameters.high_shelf_gain_db, -24.0, 24.0);
        parameters_ = parameters;
    }

    void process(
        const CommRaT::Messages::AudioBlock& input,
        CommRaT::Messages::AudioBlock& output) noexcept {
        process(input, nullptr, output);
    }

    void process(
        const CommRaT::Messages::AudioBlock& input,
        const CommRaT::Messages::ParameterEventBlock* events,
        CommRaT::Messages::AudioBlock& output) noexcept {
        clear_audio_channels(output);
        copy_audio_metadata(input, output);
        if (validate_audio_block(input) != AudioBlockValidationError::None) {
            mark_invalid_audio_block(output);
            return;
        }

        const auto* valid_events = events_are_valid(events, input.frame_count)
            ? events
            : nullptr;
        if ((input.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY) != 0) {
            reset();
        }
        set_filter_coefficients(input.sample_rate_hz, input.channel_count);
        std::size_t event_index = 0;
        for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
            while (valid_events != nullptr
                && event_index < valid_events->events.size()
                && valid_events->events[event_index].sample_offset == frame) {
                set_parameter_value(
                    valid_events->events[event_index].parameter_id,
                    valid_events->events[event_index].value);
                set_filter_coefficients(
                    input.sample_rate_hz, input.channel_count);
                ++event_index;
            }
            for (uint16_t channel = 0; channel < input.channel_count; ++channel) {
                auto sample = low_filters_[channel].process(
                    input.channels[channel][frame]);
                sample = mid_filters_[channel].process(sample);
                sample = high_filters_[channel].process(sample);
                output.channels[channel].push_back(sample);
            }
        }
    }

    void reset() noexcept {
        for (std::size_t channel = 0; channel < low_filters_.size(); ++channel) {
            low_filters_[channel].reset();
            mid_filters_[channel].reset();
            high_filters_[channel].reset();
        }
    }

private:
    static bool events_are_valid(
        const CommRaT::Messages::ParameterEventBlock* block,
        uint32_t frame_count) noexcept {
        if (block == nullptr) return false;
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

    void set_filter_coefficients(
        double sample_rate_hz,
        uint16_t channel_count) noexcept {
        const auto low = make_biquad_coefficients(
            BiquadType::LowShelf, sample_rate_hz,
            parameters_.low_shelf_frequency_hz, 0.7071067811865476,
            parameters_.low_shelf_gain_db);
        const auto mid = make_biquad_coefficients(
            BiquadType::Peaking, sample_rate_hz,
            parameters_.mid_frequency_hz, parameters_.mid_q,
            parameters_.mid_gain_db);
        const auto high = make_biquad_coefficients(
            BiquadType::HighShelf, sample_rate_hz,
            parameters_.high_shelf_frequency_hz, 0.7071067811865476,
            parameters_.high_shelf_gain_db);
        for (uint16_t channel = 0; channel < channel_count; ++channel) {
            low_filters_[channel].set_coefficients(low);
            mid_filters_[channel].set_coefficients(mid);
            high_filters_[channel].set_coefficients(high);
        }
    }

    void set_parameter_value(uint32_t parameter_id, double value) noexcept {
        if (!std::isfinite(value)) return;
        auto parameters = parameters_;
        switch (parameter_id) {
        case CommRaT::Parameters::PARAMETRIC_EQ_LOW_FREQUENCY_PARAMETER_ID:
            parameters.low_shelf_frequency_hz = value;
            break;
        case CommRaT::Parameters::PARAMETRIC_EQ_LOW_GAIN_PARAMETER_ID:
            parameters.low_shelf_gain_db = value;
            break;
        case CommRaT::Parameters::PARAMETRIC_EQ_MID_FREQUENCY_PARAMETER_ID:
            parameters.mid_frequency_hz = value;
            break;
        case CommRaT::Parameters::PARAMETRIC_EQ_MID_GAIN_PARAMETER_ID:
            parameters.mid_gain_db = value;
            break;
        case CommRaT::Parameters::PARAMETRIC_EQ_MID_Q_PARAMETER_ID:
            parameters.mid_q = value;
            break;
        case CommRaT::Parameters::PARAMETRIC_EQ_HIGH_FREQUENCY_PARAMETER_ID:
            parameters.high_shelf_frequency_hz = value;
            break;
        case CommRaT::Parameters::PARAMETRIC_EQ_HIGH_GAIN_PARAMETER_ID:
            parameters.high_shelf_gain_db = value;
            break;
        default:
            return;
        }
        set_parameters(parameters);
    }

    static double finite_or(double value, double fallback) noexcept {
        return std::isfinite(value) ? value : fallback;
    }

    CommRaT::Parameters::ParametricEqualizer parameters_{};
    std::array<BiquadSection<Sample>, CommRaT::Messages::AudioBlock::MAX_CHANNELS>
        low_filters_{};
    std::array<BiquadSection<Sample>, CommRaT::Messages::AudioBlock::MAX_CHANNELS>
        mid_filters_{};
    std::array<BiquadSection<Sample>, CommRaT::Messages::AudioBlock::MAX_CHANNELS>
        high_filters_{};
};

} // namespace musicrat::dsp
