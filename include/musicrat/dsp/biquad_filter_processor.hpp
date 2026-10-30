#pragma once

#include <musicrat/dsp/biquad.hpp>
#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/biquad_filter.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace musicrat::dsp {

class BiquadFilterProcessor {
public:
    using Sample = CommRaT::Messages::AudioBlock::Sample;

    explicit BiquadFilterProcessor(
        const CommRaT::Parameters::BiquadFilter& parameters = {}) noexcept {
        set_parameters(parameters);
    }

    void set_parameters(
        CommRaT::Parameters::BiquadFilter parameters) noexcept {
        if (parameters.filter_type > CommRaT::Parameters::BIQUAD_FILTER_HIGH_SHELF) {
            parameters.filter_type = CommRaT::Parameters::BIQUAD_FILTER_LOW_PASS;
        }
        if (!std::isfinite(parameters.frequency_hz)) {
            parameters.frequency_hz = 1000.0;
        }
        if (!std::isfinite(parameters.q)) {
            parameters.q = 0.7071067811865476;
        }
        if (!std::isfinite(parameters.gain_db)) {
            parameters.gain_db = 0.0;
        }
        parameters.frequency_hz = std::max(parameters.frequency_hz, 0.1);
        parameters.q = std::clamp(parameters.q, 0.1, 20.0);
        parameters.gain_db = std::clamp(parameters.gain_db, -24.0, 24.0);
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

        const auto type = to_biquad_type(parameters_.filter_type);
        const auto coefficients = make_biquad_coefficients(
            type, input.sample_rate_hz, parameters_.frequency_hz,
            parameters_.q, parameters_.gain_db);
        for (uint16_t channel = 0; channel < input.channel_count; ++channel) {
            filters_[channel].set_coefficients(coefficients);
            if ((input.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY) != 0) {
                filters_[channel].reset();
            }
            for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
                output.channels[channel].push_back(
                    filters_[channel].process(input.channels[channel][frame]));
            }
        }
    }

    void reset() noexcept {
        for (auto& filter : filters_) {
            filter.reset();
        }
    }

private:
    static BiquadType to_biquad_type(uint8_t type) noexcept {
        using namespace CommRaT::Parameters;
        switch (type) {
        case BIQUAD_FILTER_HIGH_PASS:
            return BiquadType::HighPass;
        case BIQUAD_FILTER_BAND_PASS:
            return BiquadType::BandPass;
        case BIQUAD_FILTER_NOTCH:
            return BiquadType::Notch;
        case BIQUAD_FILTER_PEAKING:
            return BiquadType::Peaking;
        case BIQUAD_FILTER_LOW_SHELF:
            return BiquadType::LowShelf;
        case BIQUAD_FILTER_HIGH_SHELF:
            return BiquadType::HighShelf;
        case BIQUAD_FILTER_LOW_PASS:
        default:
            return BiquadType::LowPass;
        }
    }

    CommRaT::Parameters::BiquadFilter parameters_{};
    std::array<BiquadSection<Sample>, CommRaT::Messages::AudioBlock::MAX_CHANNELS>
        filters_{};
};

} // namespace musicrat::dsp
