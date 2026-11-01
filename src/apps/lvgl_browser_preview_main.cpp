#include <musicrat/backends/lvgl_directory_browser.hpp>
#include <musicrat/backends/lvgl_display_backend.hpp>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
    using namespace musicrat::backends;
    const char* root = nullptr;
    uint32_t duration_ms = 30000;
    auto backend = LvglDisplayBackend::Headless;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--sdl") backend = LvglDisplayBackend::Sdl;
        else if (argument == "--root" && index + 1 < argc) root = argv[++index];
        else if (argument == "--duration-ms" && index + 1 < argc) {
            const std::string_view value{argv[++index]};
            const auto result = std::from_chars(value.data(), value.data() + value.size(), duration_ms);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()
                || duration_ms == 0 || duration_ms > 86400000) {
                std::fprintf(stderr, "Invalid duration (expected 1..86400000 ms)\n");
                return 1;
            }
        } else {
            std::fprintf(stderr, "Unknown or incomplete option: %s\n", argv[index]);
            return 1;
        }
    }
    if (root == nullptr || root[0] == '\0') {
        std::fprintf(stderr, "Usage: musicrat_lvgl_browser_preview --root PATH [--sdl] [--duration-ms N]\n");
        return 1;
    }
    if (!lvgl_display_backend_available(backend)) {
        std::fprintf(stderr, "Requested LVGL backend is not compiled in\n");
        return 1;
    }
    lv_init();
    auto display = create_lvgl_display({
        .backend = backend, .device_path = "", .input_device_path = "",
        .title = "MusicRaT browser preview (no playback)",
    });
    if (display.display == nullptr) {
        std::fprintf(stderr, "Cannot create LVGL display\n");
        return 1;
    }
    int exit_code = 0;
    {
        auto worker = std::make_unique<media::DirectoryBrowserWorker>(root);
        auto browser = std::make_unique<LvglDirectoryBrowser>(
            lv_display_get_screen_active(display.display), *worker);
        const auto start = std::chrono::steady_clock::now();
        auto previous = start;
        bool reported = false;
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds{duration_ms}) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - previous);
            lv_tick_inc(static_cast<uint32_t>(elapsed.count()));
            previous += elapsed;
            browser->poll();
            if (!browser->pending() && !reported) {
                reported = true;
                exit_code = browser->page().error == CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE ? 0 : 1;
                std::fprintf(stderr, "Browser result: entries=%u error=%u (no player connected)\n",
                    browser->page().total_entries, browser->page().error);
                if (exit_code != 0) break;
            }
            (void)lv_timer_handler();
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        if (!reported) {
            std::fprintf(stderr, "Browser request did not complete before preview deadline\n");
            exit_code = 1;
        }
    }
    destroy_lvgl_display(display, backend);
    return exit_code;
}
