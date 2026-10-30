#pragma once

#include <musicrat/dsp/adsr_envelope.hpp>
#include <musicrat/protocol/three_oscillator.hpp>

#include <algorithm>
#include <cmath>

namespace musicrat::dsp {

template <typename Parameters>
void normalize_note_synth_parameters(Parameters& parameters) noexcept {
    const bool legacy_envelope = parameters.envelope_ms != 5.0
        && parameters.amplitude_attack_ms == 5.0
        && parameters.amplitude_decay_ms == 0.0
        && parameters.amplitude_sustain_level == 1.0
        && parameters.amplitude_release_ms == 5.0;
    if (!std::isfinite(parameters.envelope_ms)) {
        parameters.envelope_ms = 5.0;
    }
    parameters.envelope_ms = std::clamp(parameters.envelope_ms, 1.0, 100.0);

    if (legacy_envelope) {
        parameters.amplitude_attack_ms = parameters.envelope_ms;
        parameters.amplitude_release_ms = parameters.envelope_ms;
    }
    if (!std::isfinite(parameters.amplitude_attack_ms)) {
        parameters.amplitude_attack_ms = 5.0;
    }
    if (!std::isfinite(parameters.amplitude_decay_ms)) {
        parameters.amplitude_decay_ms = 0.0;
    }
    if (!std::isfinite(parameters.amplitude_sustain_level)) {
        parameters.amplitude_sustain_level = 1.0;
    }
    if (!std::isfinite(parameters.amplitude_release_ms)) {
        parameters.amplitude_release_ms = 5.0;
    }
    parameters.amplitude_attack_ms = std::clamp(
        parameters.amplitude_attack_ms, 0.0, 10000.0);
    parameters.amplitude_decay_ms = std::clamp(
        parameters.amplitude_decay_ms, 0.0, 10000.0);
    parameters.amplitude_sustain_level = std::clamp(
        parameters.amplitude_sustain_level, 0.0, 1.0);
    parameters.amplitude_release_ms = std::clamp(
        parameters.amplitude_release_ms, 0.0, 10000.0);

    if (parameters.filter_mode
        > CommRaT::Parameters::SYNTH_FILTER_BAND_PASS) {
        parameters.filter_mode = CommRaT::Parameters::SYNTH_FILTER_BYPASS;
    }
    if (!std::isfinite(parameters.filter_cutoff_hz)) {
        parameters.filter_cutoff_hz = 1000.0;
    }
    if (!std::isfinite(parameters.filter_resonance)) {
        parameters.filter_resonance = 0.7071067811865476;
    }
    parameters.filter_cutoff_hz = std::clamp(
        parameters.filter_cutoff_hz, 1.0, parameters.sample_rate_hz * 0.49);
    parameters.filter_resonance = std::clamp(
        parameters.filter_resonance, 0.1, 20.0);

    if (!std::isfinite(parameters.filter_attack_ms)) {
        parameters.filter_attack_ms = 5.0;
    }
    if (!std::isfinite(parameters.filter_decay_ms)) {
        parameters.filter_decay_ms = 0.0;
    }
    if (!std::isfinite(parameters.filter_sustain_level)) {
        parameters.filter_sustain_level = 1.0;
    }
    if (!std::isfinite(parameters.filter_release_ms)) {
        parameters.filter_release_ms = 5.0;
    }
    if (!std::isfinite(parameters.filter_env_amount_octaves)) {
        parameters.filter_env_amount_octaves = 0.0;
    }
    parameters.filter_attack_ms = std::clamp(
        parameters.filter_attack_ms, 0.0, 10000.0);
    parameters.filter_decay_ms = std::clamp(
        parameters.filter_decay_ms, 0.0, 10000.0);
    parameters.filter_sustain_level = std::clamp(
        parameters.filter_sustain_level, 0.0, 1.0);
    parameters.filter_release_ms = std::clamp(
        parameters.filter_release_ms, 0.0, 10000.0);
    parameters.filter_env_amount_octaves = std::clamp(
        parameters.filter_env_amount_octaves, -4.0, 4.0);
}

template <typename Parameters>
[[nodiscard]] AdsrParameters amplitude_adsr(
    const Parameters& parameters) noexcept {
    return {
        parameters.amplitude_attack_ms,
        parameters.amplitude_decay_ms,
        parameters.amplitude_sustain_level,
        parameters.amplitude_release_ms,
    };
}

template <typename Parameters>
[[nodiscard]] AdsrParameters filter_adsr(
    const Parameters& parameters) noexcept {
    return {
        parameters.filter_attack_ms,
        parameters.filter_decay_ms,
        parameters.filter_sustain_level,
        parameters.filter_release_ms,
    };
}

} // namespace musicrat::dsp
