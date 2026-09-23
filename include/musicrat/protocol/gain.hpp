#pragma once

#include <musicrat/config.hpp>

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t GAIN_PARAMETER_ID = 1;
inline constexpr uint32_t GAIN_SMOOTHING_PARAMETER_ID = 2;
inline constexpr uint32_t GAIN_MUTED_PARAMETER_ID = 3;
inline constexpr uint32_t GAIN_INVERT_POLARITY_PARAMETER_ID = 4;

struct Gain {
    musicrat::config::sample_type gain{1.0};
    uint32_t smoothing_samples{64};
    bool muted{false};
    bool invert_polarity{false};
};

} // namespace CommRaT::Parameters