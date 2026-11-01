#include <musicrat/backends/lvgl_directory_browser.hpp>

#include "../support/directory_fixture.hpp"

#include <cassert>
#include <chrono>
#include <string_view>
#include <thread>

namespace {

using namespace CommRaT::Messages;
using musicrat::backends::LvglDirectoryBrowser;

lv_obj_t* find_button(lv_obj_t* object, std::string_view text) {
    if (lv_obj_check_type(object, &lv_button_class)) {
        auto* label = lv_obj_get_child(object, 0);
        if (label != nullptr && std::string_view{lv_label_get_text(label)} == text) return object;
    }
    for (uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
        if (auto* found = find_button(lv_obj_get_child(object, index), text)) return found;
    }
    return nullptr;
}

bool has_text(lv_obj_t* object, std::string_view text) {
    if (lv_obj_check_type(object, &lv_label_class)
        && std::string_view{lv_label_get_text(object)} == text) return true;
    for (uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
        if (has_text(lv_obj_get_child(object, index), text)) return true;
    }
    return false;
}

void click(LvglDirectoryBrowser& browser, const char* text) {
    auto* button = find_button(browser.object(), text);
    assert(button != nullptr);
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
}

void await(LvglDirectoryBrowser& browser) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (browser.pending()) {
        assert(std::chrono::steady_clock::now() < deadline);
        browser.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    lv_obj_update_layout(browser.object());
}

struct Selection {
    uint32_t calls{0};
    uint64_t snapshot{0};
    MediaDirectoryEntry entry{};
    bool accept{false};
};

bool selected(void* context, const MediaSelectionResult& selection) noexcept {
    auto& result = *static_cast<Selection*>(context);
    ++result.calls;
    result.snapshot = selection.snapshot_id;
    result.entry = selection.entry;
    assert(selection.error == MEDIA_BROWSE_ERROR_NONE && !selection.resolved_path.empty());
    return result.accept;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using musicrat::backends::media::DirectoryBrowserWorker;
    musicrat::testing::TestDirectory fixture;
    fs::create_directories(fixture.path / "album" / "disc");
    musicrat::testing::make_file(fixture.path / "album" / "song.wav");
    musicrat::testing::make_file(fixture.path / "album" / "disc" / "deep.wav");
    for (uint32_t index = 0; index < 19; ++index) {
        musicrat::testing::make_file(fixture.path / ("track-" + std::to_string(index)));
    }
    fs::create_symlink(fixture.path / "missing", fixture.path / "skipped-link");
    lv_init();
    auto* display = lv_display_create(800, 480);
    assert(display != nullptr);
    auto* screen = lv_obj_create(nullptr);
    {
        DirectoryBrowserWorker worker{fixture.path};
        LvglDirectoryBrowser browser{screen, worker};
        assert(browser.pending());
        assert(lv_obj_has_state(find_button(browser.object(), "Refresh"), LV_STATE_DISABLED));
        await(browser);
        assert(browser.page().total_entries == 20 && browser.page().skipped_entries == 1);
        assert(has_text(browser.object(), "1-8 of 20; 1 skipped"));
        assert(lv_obj_has_state(find_button(browser.object(), "Up"), LV_STATE_DISABLED));
        const auto root_snapshot = browser.page().snapshot_id;
        auto* list = lv_obj_get_child(browser.object(), 2);
        assert(lv_obj_get_child_count(list) == LvglDirectoryBrowser::ROW_COUNT);
        lv_area_t list_area{};
        lv_obj_get_coords(list, &list_area);
        for (uint32_t index = 0; index < LvglDirectoryBrowser::ROW_COUNT; ++index) {
            lv_area_t row_area{};
            lv_obj_get_coords(lv_obj_get_child(list, index), &row_area);
            assert(row_area.y1 >= list_area.y1 && row_area.y2 <= list_area.y2);
            assert(lv_area_get_height(&row_area) >= 20);
        }
        assert(lv_obj_get_scroll_bottom(browser.object()) == 0);
        for (uint32_t offset : {8, 16, 8, 0}) {
            click(browser, offset > browser.page().offset ? "Next" : "Previous");
            await(browser);
            assert(browser.page().offset == offset);
            assert(browser.page().snapshot_id == root_snapshot);
            assert(lv_obj_get_child_count(list) == LvglDirectoryBrowser::ROW_COUNT);
        }
        click(browser, "[Folder] album");
        await(browser);
        assert(browser.page().directory == "album");
        click(browser, "[Folder] disc");
        await(browser);
        assert(browser.page().directory == "album/disc");
        click(browser, "Up");
        await(browser);
        assert(browser.page().directory == "album");
        click(browser, "song.wav");
        assert(has_text(browser.object(), "Selection only; no load service connected"));
        auto* use = find_button(browser.object(), "Use selection");
        assert(lv_obj_has_state(use, LV_STATE_DISABLED));
        Selection selection{};
        browser.set_selection_handler(selected, &selection);
        assert(!lv_obj_has_state(use, LV_STATE_DISABLED));
        click(browser, "Use selection");
        assert(browser.pending());
        await(browser);
        assert(selection.calls == 1 && selection.entry.relative_path == "album/song.wav");
        assert(selection.snapshot == browser.page().snapshot_id);
        assert(has_text(browser.object(), "Selection rejected; retry"));
        selection.accept = true;
        click(browser, "Use selection");
        await(browser);
        assert(selection.calls == 2 && has_text(browser.object(), "Selection handed off; check load status"));
        assert(lv_obj_has_state(use, LV_STATE_DISABLED));
        click(browser, "Use selection");
        assert(selection.calls == 2);
        click(browser, "song.wav");
        fs::resize_file(fixture.path / "album" / "song.wav", 9);
        click(browser, "Use selection");
        await(browser);
        assert(selection.calls == 2);
        assert(has_text(browser.object(), "Media changed; refresh and select again"));
        click(browser, "Refresh");
        await(browser);
        click(browser, "song.wav");
        click(browser, "Use selection");
        await(browser);
        assert(selection.calls == 3 && has_text(browser.object(), "Selection handed off; check load status"));
        click(browser, "song.wav");
        click(browser, "Up");
        assert(lv_obj_has_state(use, LV_STATE_DISABLED));
        click(browser, "song.wav");
        assert(selection.calls == 3);
        await(browser);
        // The removed folder remains in this snapshot; entering it must show a failure, not stale rows.
        fs::remove_all(fixture.path / "album");
        click(browser, "[Folder] album");
        await(browser);
        assert(browser.page().error == MEDIA_BROWSE_ERROR_NOT_FOUND);
        assert(has_text(browser.object(), "Directory removed or not found"));
        for (uint32_t index = 0; index < LvglDirectoryBrowser::ROW_COUNT; ++index) {
            assert(lv_obj_has_flag(lv_obj_get_child(list, index), LV_OBJ_FLAG_HIDDEN));
        }
        click(browser, "Up");
        await(browser);
        assert(browser.page().error == MEDIA_BROWSE_ERROR_NONE);
        assert(browser.page().total_entries == 19);
        click(browser, "Refresh");
        await(browser);
        assert(browser.page().snapshot_id != root_snapshot);
    }
    {
        DirectoryBrowserWorker worker{fixture.path / "missing"};
        LvglDirectoryBrowser browser{screen, worker};
        await(browser);
        assert(has_text(browser.object(), "Directory removed or not found"));
    }
    {
        fs::create_directory(fixture.path / "empty");
        DirectoryBrowserWorker worker{fixture.path / "empty"};
        LvglDirectoryBrowser browser{screen, worker};
        await(browser);
        assert(has_text(browser.object(), "Empty directory"));
        assert(lv_obj_has_state(find_button(browser.object(), "Next"), LV_STATE_DISABLED));
    }
    lv_obj_delete(screen);
    lv_display_delete(display);
}
