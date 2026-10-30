#pragma once

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t BIQUAD_FILTER_TYPE_PARAMETER_ID = 1;
inline constexpr uint32_t BIQUAD_FILTER_FREQUENCY_PARAMETER_ID = 2;
inline constexpr uint32_t BIQUAD_FILTER_Q_PARAMETER_ID = 3;
inline constexpr uint32_t BIQUAD_FILTER_GAIN_PARAMETER_ID = 4;

enum BiquadFilterType : uint8_t {
    BIQUAD_FILTER_LOW_PASS = 0,
    BIQUAD_FILTER_HIGH_PASS = 1,
    BIQUAD_FILTER_BAND_PASS = 2,
    BIQUAD_FILTER_NOTCH = 3,
    BIQUAD_FILTER_PEAKING = 4,
    BIQUAD_FILTER_LOW_SHELF = 5,
    BIQUAD_FILTER_HIGH_SHELF = 6,
};

struct BiquadFilter {
    uint8_t filter_type{BIQUAD_FILTER_LOW_PASS};
    double frequency_hz{1000.0};
    double q{0.7071067811865476};
    double gain_db{0.0};
};

} // namespace CommRaT::Parameters
