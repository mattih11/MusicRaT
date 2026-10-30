#pragma once

#include <musicrat/config.hpp>

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t THREE_OSCILLATOR_SAMPLE_RATE_PARAMETER_ID = 1;
inline constexpr uint32_t THREE_OSCILLATOR_MASTER_GAIN_PARAMETER_ID = 2;
inline constexpr uint32_t THREE_OSCILLATOR_ENVELOPE_PARAMETER_ID = 3;
inline constexpr uint32_t THREE_OSCILLATOR_1_WAVEFORM_PARAMETER_ID = 4;
inline constexpr uint32_t THREE_OSCILLATOR_1_SEMITONES_PARAMETER_ID = 5;
inline constexpr uint32_t THREE_OSCILLATOR_1_LEVEL_PARAMETER_ID = 6;
inline constexpr uint32_t THREE_OSCILLATOR_2_WAVEFORM_PARAMETER_ID = 7;
inline constexpr uint32_t THREE_OSCILLATOR_2_SEMITONES_PARAMETER_ID = 8;
inline constexpr uint32_t THREE_OSCILLATOR_2_LEVEL_PARAMETER_ID = 9;
inline constexpr uint32_t THREE_OSCILLATOR_3_WAVEFORM_PARAMETER_ID = 10;
inline constexpr uint32_t THREE_OSCILLATOR_3_SEMITONES_PARAMETER_ID = 11;
inline constexpr uint32_t THREE_OSCILLATOR_3_LEVEL_PARAMETER_ID = 12;
inline constexpr uint32_t THREE_OSCILLATOR_AMP_ATTACK_PARAMETER_ID = 13;
inline constexpr uint32_t THREE_OSCILLATOR_AMP_DECAY_PARAMETER_ID = 14;
inline constexpr uint32_t THREE_OSCILLATOR_AMP_SUSTAIN_PARAMETER_ID = 15;
inline constexpr uint32_t THREE_OSCILLATOR_AMP_RELEASE_PARAMETER_ID = 16;
inline constexpr uint32_t THREE_OSCILLATOR_FILTER_MODE_PARAMETER_ID = 17;
inline constexpr uint32_t THREE_OSCILLATOR_FILTER_CUTOFF_PARAMETER_ID = 18;
inline constexpr uint32_t THREE_OSCILLATOR_FILTER_RESONANCE_PARAMETER_ID = 19;
inline constexpr uint32_t THREE_OSCILLATOR_FILTER_ATTACK_PARAMETER_ID = 20;
inline constexpr uint32_t THREE_OSCILLATOR_FILTER_DECAY_PARAMETER_ID = 21;
inline constexpr uint32_t THREE_OSCILLATOR_FILTER_SUSTAIN_PARAMETER_ID = 22;
inline constexpr uint32_t THREE_OSCILLATOR_FILTER_RELEASE_PARAMETER_ID = 23;
inline constexpr uint32_t THREE_OSCILLATOR_FILTER_ENV_AMOUNT_PARAMETER_ID = 24;

enum SynthWaveform : uint8_t {
    SYNTH_WAVEFORM_SINE = 0,
    SYNTH_WAVEFORM_SQUARE = 1,
    SYNTH_WAVEFORM_TRIANGLE = 2,
    SYNTH_WAVEFORM_SAWTOOTH = 3,
    SYNTH_WAVEFORM_REVERSE_SAWTOOTH = 4,
};

enum SynthFilterMode : uint8_t {
    SYNTH_FILTER_BYPASS = 0,
    SYNTH_FILTER_LOW_PASS = 1,
    SYNTH_FILTER_HIGH_PASS = 2,
    SYNTH_FILTER_BAND_PASS = 3,
};

struct ThreeOscillator {
    double sample_rate_hz{musicrat::config::default_sample_rate_hz};
    musicrat::config::sample_type master_gain{0.2};
    double envelope_ms{5.0};
    uint8_t oscillator_1_waveform{SYNTH_WAVEFORM_SINE};
    double oscillator_1_semitones{0.0};
    musicrat::config::sample_type oscillator_1_level{0.33};
    uint8_t oscillator_2_waveform{SYNTH_WAVEFORM_SQUARE};
    double oscillator_2_semitones{0.0};
    musicrat::config::sample_type oscillator_2_level{0.0};
    uint8_t oscillator_3_waveform{SYNTH_WAVEFORM_TRIANGLE};
    double oscillator_3_semitones{0.0};
    musicrat::config::sample_type oscillator_3_level{0.0};
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
