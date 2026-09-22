#pragma once

#include <musicrat/config.hpp>

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t GAIN_PARAMETER_ID = 1;

struct Gain {
    musicrat::config::sample_type gain{1.0};
    uint32_t smoothing_samples{64};
    bool muted{false};
    bool invert_polarity{false};
};

struct ParameterSource {
    uint32_t source_endpoint_id{1};
    uint32_t parameter_id{GAIN_PARAMETER_ID};
    double value{1.0};
};

} // namespace CommRaT::Parameters