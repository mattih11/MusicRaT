#include <musicrat/backends/lvgl_display_backend.hpp>

#include <string>

namespace musicrat::backends {

bool lvgl_display_backend_available(LvglDisplayBackend backend) noexcept {
    switch (backend) {
    case LvglDisplayBackend::Headless:
        return true;
    case LvglDisplayBackend::Drm:
#if MUSICRAT_LVGL_HAS_DRM
        return true;
#else
        return false;
#endif
    case LvglDisplayBackend::Framebuffer:
#if MUSICRAT_LVGL_HAS_FBDEV
        return true;
#else
        return false;
#endif
        case LvglDisplayBackend::Sdl:
    #if MUSICRAT_LVGL_HAS_SDL
        return true;
    #else
        return false;
    #endif
    }
    return false;
}

LvglDisplayHandle create_lvgl_display(
    const LvglDisplayConfig& config) noexcept {
    lv_display_t* display = nullptr;
    switch (config.backend) {
    case LvglDisplayBackend::Headless:
        if (config.width == 0 || config.height == 0) return {};
        display = lv_display_create(
            static_cast<int32_t>(config.width),
            static_cast<int32_t>(config.height));
        break;
    case LvglDisplayBackend::Drm:
#if MUSICRAT_LVGL_HAS_DRM
    {
        display = lv_linux_drm_create();
        if (display == nullptr) return {};
        char* discovered_path = nullptr;
        const char* device_path = nullptr;
        std::string configured_path;
        if (config.device_path.empty()) {
            discovered_path = lv_linux_drm_find_device_path();
            device_path = discovered_path;
        } else {
            configured_path = config.device_path;
            device_path = configured_path.c_str();
        }
        const auto result = device_path == nullptr
            ? LV_RESULT_INVALID
            : lv_linux_drm_set_file(
                display, device_path, config.connector_id);
        if (discovered_path != nullptr) lv_free(discovered_path);
        if (result != LV_RESULT_OK) {
            lv_display_delete(display);
            return {};
        }
        break;
    }
#else
        return {};
#endif
    case LvglDisplayBackend::Framebuffer:
#if MUSICRAT_LVGL_HAS_FBDEV
    {
        display = lv_linux_fbdev_create();
        if (display == nullptr) return {};
        const std::string device_path = config.device_path.empty()
            ? "/dev/fb0"
            : std::string{config.device_path};
        if (lv_linux_fbdev_set_file(display, device_path.c_str())
            != LV_RESULT_OK) {
            lv_display_delete(display);
            return {};
        }
        break;
    }
#else
        return {};
#endif
    case LvglDisplayBackend::Sdl:
#if MUSICRAT_LVGL_HAS_SDL
    {
        if (config.width == 0 || config.height == 0) return {};
        display = lv_sdl_window_create(
            static_cast<int32_t>(config.width),
            static_cast<int32_t>(config.height));
        if (display == nullptr) return {};
        const std::string title = config.title.empty()
            ? "MusicRaT"
            : std::string{config.title};
        lv_sdl_window_set_title(display, title.c_str());
        lv_sdl_window_set_resizeable(display, true);

        auto* mouse = lv_sdl_mouse_create();
        if (mouse != nullptr) lv_indev_set_display(mouse, display);
        auto* keyboard = lv_sdl_keyboard_create();
        if (keyboard != nullptr) lv_indev_set_display(keyboard, display);
        break;
    }
#else
        return {};
#endif
    }

    if (display == nullptr) return {};
    return {
        .display = display,
        .width = lv_display_get_horizontal_resolution(display),
        .height = lv_display_get_vertical_resolution(display),
    };
}

void destroy_lvgl_display(
    LvglDisplayHandle& handle,
    LvglDisplayBackend backend) noexcept {
    if (handle.display == nullptr) return;
    lv_display_delete(handle.display);
    handle = {};
#if MUSICRAT_LVGL_HAS_SDL
    if (backend == LvglDisplayBackend::Sdl) lv_sdl_quit();
#else
    static_cast<void>(backend);
#endif
}

} // namespace musicrat::backends