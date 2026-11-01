#pragma once

#include <sertial/containers/fixed_string.hpp>
#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t LVGL_DECK_ROOT_PARAMETER_ID = 1;
inline constexpr uint32_t LVGL_DECK_BACKEND_PARAMETER_ID = 2;
inline constexpr uint32_t LVGL_DECK_TIMEOUT_PARAMETER_ID = 3;

struct LvglDeck {
    sertial::fixed_string<512> media_root{"."};
    uint8_t backend{0};
    uint32_t load_timeout_ms{10000};
};

} // namespace CommRaT::Parameters
