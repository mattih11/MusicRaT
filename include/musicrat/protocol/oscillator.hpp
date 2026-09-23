#pragma once

#include <musicrat/config.hpp>

#include <cstdint>

namespace CommRaT::Messages {

struct ResetPhase {
    struct Reply {
        bool success{false};
    };
};

} // namespace CommRaT::Messages

namespace CommRaT::Parameters {

inline constexpr uint32_t OSCILLATOR_FREQUENCY_PARAMETER_ID = 1;
inline constexpr uint32_t OSCILLATOR_AMPLITUDE_PARAMETER_ID = 2;
inline constexpr uint32_t OSCILLATOR_SAMPLE_RATE_PARAMETER_ID = 3;
inline constexpr uint32_t OSCILLATOR_PHASE_OFFSET_PARAMETER_ID = 4;
inline constexpr uint32_t OSCILLATOR_ENABLED_PARAMETER_ID = 5;

struct Oscillator {
    double frequency_hz{440.0};
    musicrat::config::sample_type amplitude{1.0};
    double sample_rate_hz{musicrat::config::default_sample_rate_hz};
    double phase_offset{0.0};
    bool enabled{true};
};

} // namespace CommRaT::Parameters