#pragma once

#include <musicrat/config.hpp>
#include <musicrat/protocol/stereo_panner.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace musicrat::dsp {

struct StereoPanGains {
    musicrat::config::sample_type left{1.0};
    musicrat::config::sample_type right{1.0};
};

[[nodiscard]] inline StereoPanGains stereo_pan_gains(
    musicrat::config::sample_type pan,
    CommRaT::Parameters::PanLaw pan_law) noexcept {
    using Sample = musicrat::config::sample_type;

    if (!std::isfinite(pan)) {
        pan = Sample{0};
    }
    pan = std::clamp(pan, Sample{-1}, Sample{1});

    if (pan_law == CommRaT::Parameters::PAN_LAW_LINEAR) {
        return {
            .left = (Sample{1} - pan) / Sample{2},
            .right = (Sample{1} + pan) / Sample{2},
        };
    }

    const auto angle = (pan + Sample{1})
        * std::numbers::pi_v<Sample> / Sample{4};
    return {
        .left = std::cos(angle),
        .right = std::sin(angle),
    };
}

} // namespace musicrat::dsp