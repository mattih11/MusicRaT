#pragma once

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t PARAMETRIC_EQ_LOW_FREQUENCY_PARAMETER_ID = 1;
inline constexpr uint32_t PARAMETRIC_EQ_LOW_GAIN_PARAMETER_ID = 2;
inline constexpr uint32_t PARAMETRIC_EQ_MID_FREQUENCY_PARAMETER_ID = 3;
inline constexpr uint32_t PARAMETRIC_EQ_MID_GAIN_PARAMETER_ID = 4;
inline constexpr uint32_t PARAMETRIC_EQ_MID_Q_PARAMETER_ID = 5;
inline constexpr uint32_t PARAMETRIC_EQ_HIGH_FREQUENCY_PARAMETER_ID = 6;
inline constexpr uint32_t PARAMETRIC_EQ_HIGH_GAIN_PARAMETER_ID = 7;

struct ParametricEqualizer {
    double low_shelf_frequency_hz{120.0};
    double low_shelf_gain_db{0.0};
    double mid_frequency_hz{1000.0};
    double mid_gain_db{0.0};
    double mid_q{0.7071067811865476};
    double high_shelf_frequency_hz{8000.0};
    double high_shelf_gain_db{0.0};
};

} // namespace CommRaT::Parameters
