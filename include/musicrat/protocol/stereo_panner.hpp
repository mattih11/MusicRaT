#pragma once

#include <musicrat/config.hpp>

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t PAN_PARAMETER_ID = 1;

using PanLaw = uint8_t;

inline constexpr PanLaw PAN_LAW_LINEAR = 0;
inline constexpr PanLaw PAN_LAW_EQUAL_POWER = 1;

struct StereoPanner {
    musicrat::config::sample_type pan{0.0};
    PanLaw pan_law{PAN_LAW_EQUAL_POWER};
    uint32_t smoothing_samples{64};
};

} // namespace CommRaT::Parameters