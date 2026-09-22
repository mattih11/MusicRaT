#pragma once

#include <musicrat/config.hpp>

namespace CommRaT::Messages {

struct ResetPhase {
    struct Reply {
        bool success{false};
    };
};

} // namespace CommRaT::Messages

namespace CommRaT::Parameters {

struct Oscillator {
    double frequency_hz{440.0};
    musicrat::config::sample_type amplitude{1.0};
    double sample_rate_hz{musicrat::config::default_sample_rate_hz};
    double phase_offset{0.0};
    bool enabled{true};
};

} // namespace CommRaT::Parameters