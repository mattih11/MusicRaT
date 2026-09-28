#pragma once

#include <lvgl.h>

#include <cstdint>
#include <string_view>

namespace musicrat::backends {

enum class LvglDisplayBackend : uint8_t {
    Headless = 0,
    Drm = 1,
    Framebuffer = 2,
    Sdl = 3,
};

struct LvglDisplayConfig {
    LvglDisplayBackend backend{LvglDisplayBackend::Headless};
    std::string_view device_path;
    int64_t connector_id{-1};
    uint32_t width{800};
    uint32_t height{480};
    std::string_view title{"MusicRaT"};
};

struct LvglDisplayHandle {
    lv_display_t* display{nullptr};
    int32_t width{0};
    int32_t height{0};
};

[[nodiscard]] bool lvgl_display_backend_available(
    LvglDisplayBackend backend) noexcept;

[[nodiscard]] LvglDisplayHandle create_lvgl_display(
    const LvglDisplayConfig& config) noexcept;

void destroy_lvgl_display(
    LvglDisplayHandle& handle,
    LvglDisplayBackend backend) noexcept;

} // namespace musicrat::backends