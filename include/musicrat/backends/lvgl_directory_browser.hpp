#pragma once

#include <musicrat/backends/media/directory_browser_worker.hpp>

#include <lvgl.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace musicrat::backends {

// All methods, callbacks and destruction belong to the owning LVGL thread.
class LvglDirectoryBrowser {
public:
    static constexpr uint32_t ROW_COUNT = 8;
    using SelectionHandler = bool (*)(
        void* context, const CommRaT::Messages::MediaSelectionResult& selection) noexcept;

    LvglDirectoryBrowser(lv_obj_t* parent, media::DirectoryBrowserWorker& worker);
    ~LvglDirectoryBrowser();

    LvglDirectoryBrowser(const LvglDirectoryBrowser&) = delete;
    LvglDirectoryBrowser& operator=(const LvglDirectoryBrowser&) = delete;

    void set_selection_handler(SelectionHandler handler, void* context) noexcept;
    void refresh();
    void poll();

    [[nodiscard]] lv_obj_t* object() const noexcept { return object_; }
    [[nodiscard]] bool pending() const noexcept { return pending_request_id_ != 0; }
    [[nodiscard]] const CommRaT::Messages::MediaDirectoryPage& page() const noexcept { return page_; }

private:
    struct Row {
        LvglDirectoryBrowser* owner{nullptr};
        uint32_t index{0};
        lv_obj_t* button{nullptr};
        lv_obj_t* label{nullptr};
    };

    static void row_clicked(lv_event_t* event);
    static void navigation_clicked(lv_event_t* event);
    void browse(const sertial::fixed_string<512>& directory);
    void request_page(uint32_t offset);
    void validate_selection();
    void select(uint32_t index);
    void update_controls();
    void show_page();
    void show_error(CommRaT::Messages::MediaBrowseError error);
    [[nodiscard]] uint64_t next_request_id();

    media::DirectoryBrowserWorker& worker_;
    lv_obj_t* object_{nullptr};
    lv_obj_t* directory_label_{nullptr};
    lv_obj_t* status_label_{nullptr};
    lv_obj_t* selection_label_{nullptr};
    lv_obj_t* up_{nullptr};
    lv_obj_t* refresh_{nullptr};
    lv_obj_t* previous_{nullptr};
    lv_obj_t* next_{nullptr};
    lv_obj_t* use_selection_{nullptr};
    std::array<Row, ROW_COUNT> rows_{};
    CommRaT::Messages::MediaDirectoryPage page_{};
    sertial::fixed_string<512> requested_directory_{};
    uint64_t request_counter_{0};
    uint64_t pending_request_id_{0};
    bool pending_selection_{false};
    uint32_t selected_row_{ROW_COUNT};
    SelectionHandler selection_handler_{nullptr};
    void* selection_context_{nullptr};
};

} // namespace musicrat::backends
