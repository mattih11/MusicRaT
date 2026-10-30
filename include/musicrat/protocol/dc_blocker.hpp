#pragma once

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t DC_BLOCKER_CUTOFF_PARAMETER_ID = 1;

struct DcBlocker {
    double cutoff_hz{20.0};
};

} // namespace CommRaT::Parameters
