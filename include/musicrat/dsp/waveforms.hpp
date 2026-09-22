#pragma once

#include <sertial/containers/fixed_string.hpp>

#include <cmath>
#include <numbers>

namespace CommRaT::Waveforms {

struct Sin {
    static constexpr sertial::fixed_string name = "Sine";

    double operator()(double phase) const {
        return std::sin(2.0 * std::numbers::pi * phase);
    }
};

struct Square {
    static constexpr sertial::fixed_string name = "Square";

    double operator()(double phase) const {
        return (phase < 0.5) ? 1.0 : -1.0;
    }
};

struct Triangle {
    static constexpr sertial::fixed_string name = "Triangle";

    double operator()(double phase) const {
        if (phase < 0.25) {
            return 4.0 * phase;
        }
        if (phase < 0.75) {
            return 2.0 - 4.0 * phase;
        }
        return 4.0 * phase - 4.0;
    }
};

struct Sawtooth {
    static constexpr sertial::fixed_string name = "Sawtooth";

    double operator()(double phase) const {
        return 2.0 * phase - 1.0;
    }
};

struct ReverseSawtooth {
    static constexpr sertial::fixed_string name = "ReverseSawtooth";

    double operator()(double phase) const {
        return 1.0 - 2.0 * phase;
    }
};

} // namespace CommRaT::Waveforms