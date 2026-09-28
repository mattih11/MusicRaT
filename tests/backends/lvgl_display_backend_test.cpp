#include <musicrat/backends/lvgl_display_backend.hpp>

#include <cassert>
#include <cstdlib>

int main() {
    using musicrat::backends::LvglDisplayBackend;
    using musicrat::backends::LvglDisplayConfig;
    using musicrat::backends::create_lvgl_display;
    using musicrat::backends::lvgl_display_backend_available;

    lv_init();

    assert(lvgl_display_backend_available(LvglDisplayBackend::Headless));
#if MUSICRAT_LVGL_HAS_DRM
    assert(lvgl_display_backend_available(LvglDisplayBackend::Drm));
#else
    assert(!lvgl_display_backend_available(LvglDisplayBackend::Drm));
#endif
#if MUSICRAT_LVGL_HAS_FBDEV
    assert(lvgl_display_backend_available(LvglDisplayBackend::Framebuffer));
#else
    assert(!lvgl_display_backend_available(LvglDisplayBackend::Framebuffer));
#endif
    assert(!lvgl_display_backend_available(
        static_cast<LvglDisplayBackend>(255)));

    const auto headless = create_lvgl_display({
        .backend = LvglDisplayBackend::Headless,
        .device_path = {},
        .connector_id = -1,
        .width = 320,
        .height = 240,
        .title = "MusicRaT Headless Test",
    });
    assert(headless.display != nullptr);
    assert(headless.width == 320);
    assert(headless.height == 240);
    lv_display_delete(headless.display);

#if MUSICRAT_LVGL_HAS_DRM
    const auto missing_drm = create_lvgl_display({
        .backend = LvglDisplayBackend::Drm,
        .device_path = "/dev/musicrat-missing-drm-device",
        .connector_id = -1,
        .width = 800,
        .height = 480,
        .title = "MusicRaT DRM Test",
    });
    assert(missing_drm.display == nullptr);
#endif

#if MUSICRAT_LVGL_HAS_FBDEV
    const auto missing_fbdev = create_lvgl_display({
        .backend = LvglDisplayBackend::Framebuffer,
        .device_path = "/dev/musicrat-missing-framebuffer",
        .connector_id = -1,
        .width = 800,
        .height = 480,
        .title = "MusicRaT Framebuffer Test",
    });
    assert(missing_fbdev.display == nullptr);
#endif

#if MUSICRAT_LVGL_HAS_SDL
    assert(lvgl_display_backend_available(LvglDisplayBackend::Sdl));
    assert(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
    auto sdl = create_lvgl_display({
        .backend = LvglDisplayBackend::Sdl,
        .device_path = {},
        .connector_id = -1,
        .width = 320,
        .height = 240,
        .title = "MusicRaT SDL Test",
    });
    assert(sdl.display != nullptr);
    assert(sdl.width == 320);
    assert(sdl.height == 240);
    musicrat::backends::destroy_lvgl_display(sdl, LvglDisplayBackend::Sdl);
    assert(sdl.display == nullptr);
#else
    assert(!lvgl_display_backend_available(LvglDisplayBackend::Sdl));
#endif
}