#pragma once

#include <musicrat/protocol/three_oscillator.hpp>

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t WAVETABLE_OSCILLATOR_SAMPLE_RATE_PARAMETER_ID = 1;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_WAVEFORM_PARAMETER_ID = 2;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_GAIN_PARAMETER_ID = 3;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_ENVELOPE_PARAMETER_ID = 4;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_AMP_ATTACK_PARAMETER_ID = 5;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_AMP_DECAY_PARAMETER_ID = 6;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_AMP_SUSTAIN_PARAMETER_ID = 7;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_AMP_RELEASE_PARAMETER_ID = 8;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_FILTER_MODE_PARAMETER_ID = 9;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_FILTER_CUTOFF_PARAMETER_ID = 10;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_FILTER_RESONANCE_PARAMETER_ID = 11;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_FILTER_ATTACK_PARAMETER_ID = 12;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_FILTER_DECAY_PARAMETER_ID = 13;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_FILTER_SUSTAIN_PARAMETER_ID = 14;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_FILTER_RELEASE_PARAMETER_ID = 15;
inline constexpr uint32_t WAVETABLE_OSCILLATOR_FILTER_ENV_AMOUNT_PARAMETER_ID = 16;

struct WavetableOscillator {
    double sample_rate_hz{musicrat::config::default_sample_rate_hz};
    uint8_t waveform{SYNTH_WAVEFORM_SINE};
    musicrat::config::sample_type gain{0.2};
    double envelope_ms{5.0};
    double amplitude_attack_ms{5.0};
    double amplitude_decay_ms{0.0};
    double amplitude_sustain_level{1.0};
    double amplitude_release_ms{5.0};
    uint8_t filter_mode{SYNTH_FILTER_BYPASS};
    double filter_cutoff_hz{1000.0};
    double filter_resonance{0.7071067811865476};
    double filter_attack_ms{5.0};
    double filter_decay_ms{0.0};
    double filter_sustain_level{1.0};
    double filter_release_ms{5.0};
    double filter_env_amount_octaves{0.0};
};

} // namespace CommRaT::Parameters
