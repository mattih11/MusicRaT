#pragma once

#include <musicrat/dsp/biquad.hpp>
#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/three_oscillator.hpp>

#include <algorithm>
#include <cmath>

namespace musicrat::dsp {

class NoteVoiceFilter {
public:
    using Sample = CommRaT::Messages::AudioBlock::Sample;

    [[nodiscard]] Sample process(
        Sample input,
        uint8_t mode,
        double cutoff_hz,
        double resonance,
        double envelope,
        double envelope_amount_octaves,
        double sample_rate_hz) noexcept {
        using namespace CommRaT::Parameters;
        if (mode == SYNTH_FILTER_BYPASS) {
            return input;
        }

        const double modulated_cutoff = std::clamp(
            cutoff_hz * std::exp2(envelope * envelope_amount_octaves),
            1.0, sample_rate_hz * 0.49);
        BiquadType type = BiquadType::LowPass;
        if (mode == SYNTH_FILTER_HIGH_PASS) {
            type = BiquadType::HighPass;
        } else if (mode == SYNTH_FILTER_BAND_PASS) {
            type = BiquadType::BandPass;
        }
        section_.set_coefficients(make_biquad_coefficients(
            type, sample_rate_hz, modulated_cutoff, resonance, 0.0));
        return section_.process(input);
    }

    void reset() noexcept {
        section_.reset();
    }

private:
    BiquadSection<Sample> section_{};
};

} // namespace musicrat::dsp
