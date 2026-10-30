#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace musicrat::dsp {

struct BiquadCoefficients {
    double b0{1.0};
    double b1{0.0};
    double b2{0.0};
    double a1{0.0};
    double a2{0.0};
};

enum class BiquadType {
    LowPass,
    HighPass,
    BandPass,
    Notch,
    Peaking,
    LowShelf,
    HighShelf,
};

[[nodiscard]] inline double bounded_frequency(
    double frequency_hz,
    double sample_rate_hz) noexcept {
    const double maximum = sample_rate_hz * 0.49;
    return std::clamp(frequency_hz, std::min(1.0, maximum), maximum);
}

[[nodiscard]] inline BiquadCoefficients make_biquad_coefficients(
    BiquadType type,
    double sample_rate_hz,
    double frequency_hz,
    double q,
    double gain_db) noexcept {
    const double frequency = bounded_frequency(frequency_hz, sample_rate_hz);
    const double bounded_q = std::clamp(q, 0.1, 20.0);
    const double bounded_gain_db = std::clamp(gain_db, -24.0, 24.0);
    const double omega = 2.0 * std::numbers::pi * frequency / sample_rate_hz;
    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    const double alpha = sine / (2.0 * bounded_q);
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a0 = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;

    switch (type) {
    case BiquadType::LowPass:
        b0 = (1.0 - cosine) * 0.5;
        b1 = 1.0 - cosine;
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha;
        break;
    case BiquadType::HighPass:
        b0 = (1.0 + cosine) * 0.5;
        b1 = -(1.0 + cosine);
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha;
        break;
    case BiquadType::BandPass:
        b0 = alpha;
        b1 = 0.0;
        b2 = -alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha;
        break;
    case BiquadType::Notch:
        b0 = 1.0;
        b1 = -2.0 * cosine;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha;
        break;
    case BiquadType::Peaking: {
        const double amplitude = std::pow(10.0, bounded_gain_db / 40.0);
        b0 = 1.0 + alpha * amplitude;
        b1 = -2.0 * cosine;
        b2 = 1.0 - alpha * amplitude;
        a0 = 1.0 + alpha / amplitude;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha / amplitude;
        break;
    }
    case BiquadType::LowShelf:
    case BiquadType::HighShelf: {
        const double amplitude = std::pow(10.0, bounded_gain_db / 40.0);
        const double shelf_alpha = sine * std::sqrt(2.0) * 0.5;
        const double beta = 2.0 * std::sqrt(amplitude) * shelf_alpha;
        if (type == BiquadType::LowShelf) {
            b0 = amplitude * ((amplitude + 1.0)
                - (amplitude - 1.0) * cosine + beta);
            b1 = 2.0 * amplitude * ((amplitude - 1.0)
                - (amplitude + 1.0) * cosine);
            b2 = amplitude * ((amplitude + 1.0)
                - (amplitude - 1.0) * cosine - beta);
            a0 = (amplitude + 1.0) + (amplitude - 1.0) * cosine + beta;
            a1 = -2.0 * ((amplitude - 1.0)
                + (amplitude + 1.0) * cosine);
            a2 = (amplitude + 1.0) + (amplitude - 1.0) * cosine - beta;
        } else {
            b0 = amplitude * ((amplitude + 1.0)
                + (amplitude - 1.0) * cosine + beta);
            b1 = -2.0 * amplitude * ((amplitude - 1.0)
                + (amplitude + 1.0) * cosine);
            b2 = amplitude * ((amplitude + 1.0)
                + (amplitude - 1.0) * cosine - beta);
            a0 = (amplitude + 1.0) - (amplitude - 1.0) * cosine + beta;
            a1 = 2.0 * ((amplitude - 1.0)
                - (amplitude + 1.0) * cosine);
            a2 = (amplitude + 1.0) - (amplitude - 1.0) * cosine - beta;
        }
        break;
    }
    }

    return {
        .b0 = b0 / a0,
        .b1 = b1 / a0,
        .b2 = b2 / a0,
        .a1 = a1 / a0,
        .a2 = a2 / a0,
    };
}

template <typename Sample>
class BiquadSection {
public:
    void set_coefficients(const BiquadCoefficients& coefficients) noexcept {
        coefficients_ = coefficients;
    }

    [[nodiscard]] Sample process(Sample input) noexcept {
        const double x = static_cast<double>(input);
        const double output = coefficients_.b0 * x + state_1_;
        state_1_ = coefficients_.b1 * x
            - coefficients_.a1 * output + state_2_;
        state_2_ = coefficients_.b2 * x - coefficients_.a2 * output;
        return static_cast<Sample>(output);
    }

    void reset() noexcept {
        state_1_ = 0.0;
        state_2_ = 0.0;
    }

private:
    BiquadCoefficients coefficients_{};
    double state_1_{0.0};
    double state_2_{0.0};
};

} // namespace musicrat::dsp
