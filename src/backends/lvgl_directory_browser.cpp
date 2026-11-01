#include <musicrat/backends/lvgl_directory_browser.hpp>

#include <limits>
#include <string_view>

namespace musicrat::backends {
namespace {

using namespace CommRaT::Messages;

lv_obj_t* button(lv_obj_t* parent, const char* text) {
    auto* object = lv_button_create(parent);
    auto* label = lv_label_create(object);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return object;
}

void enabled(lv_obj_t* object, bool value) {
    if (value) lv_obj_remove_state(object, LV_STATE_DISABLED);
    else lv_obj_add_state(object, LV_STATE_DISABLED);
}

const char* error_text(MediaBrowseError error) {
    switch (error) {
    case MEDIA_BROWSE_ERROR_INVALID_REQUEST: return "Invalid browser request";
    case MEDIA_BROWSE_ERROR_NOT_CONFIGURED: return "Media root not configured";
    case MEDIA_BROWSE_ERROR_NOT_FOUND: return "Directory removed or not found";
    case MEDIA_BROWSE_ERROR_NOT_DIRECTORY: return "Not a directory";
    case MEDIA_BROWSE_ERROR_OUTSIDE_ROOT: return "Path outside root or symlink traversal";
    case MEDIA_BROWSE_ERROR_PERMISSION_DENIED: return "Permission denied";
    case MEDIA_BROWSE_ERROR_IO: return "Directory I/O error";
    case MEDIA_BROWSE_ERROR_PATH_TOO_LONG: return "Media path too long";
    case MEDIA_BROWSE_ERROR_SCAN_LIMIT: return "Directory scan limit exceeded";
    case MEDIA_BROWSE_ERROR_STALE_SNAPSHOT: return "Directory snapshot expired; refresh";
    case MEDIA_BROWSE_ERROR_RESOURCE_EXHAUSTED: return "Browser resources exhausted";
    case MEDIA_BROWSE_ERROR_SELECTION_CHANGED: return "Media changed; refresh and select again";
    case MEDIA_BROWSE_ERROR_NOT_FILE: return "Selected entry is not a regular file";
    case MEDIA_BROWSE_ERROR_INVALID_LIBRARY: return "Rekordbox library is invalid or unsupported";
    default: return "Unknown directory error";
    }
}

} // namespace

LvglDirectoryBrowser::LvglDirectoryBrowser(lv_obj_t* parent, media::DirectoryBrowserWorker& worker)
    : worker_(worker) {
    object_ = lv_obj_create(parent);
    lv_obj_set_size(object_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(object_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(object_, 8, 0);
    lv_obj_set_style_pad_row(object_, 4, 0);
    directory_label_ = lv_label_create(object_);
    lv_obj_set_width(directory_label_, LV_PCT(100));
    lv_label_set_long_mode(directory_label_, LV_LABEL_LONG_DOT);
    lv_label_set_text(directory_label_, "/");
    auto* navigation = lv_obj_create(object_);
    lv_obj_set_size(navigation, LV_PCT(100), 42);
    lv_obj_set_style_pad_all(navigation, 0, 0);
    lv_obj_set_flex_flow(navigation, LV_FLEX_FLOW_ROW);
    up_ = button(navigation, "Up");
    refresh_ = button(navigation, "Refresh");
    previous_ = button(navigation, "Previous");
    next_ = button(navigation, "Next");
    for (auto* control : {up_, refresh_, previous_, next_}) {
        lv_obj_add_event_cb(control, navigation_clicked, LV_EVENT_CLICKED, this);
    }
    auto* list = lv_obj_create(object_);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    for (uint32_t index = 0; index < ROW_COUNT; ++index) {
        auto& row = rows_[index];
        row.owner = this;
        row.index = index;
        row.button = button(list, "");
        row.label = lv_obj_get_child(row.button, 0);
        lv_obj_set_width(row.button, LV_PCT(100));
        lv_obj_set_height(row.button, 0);
        lv_obj_set_flex_grow(row.button, 1);
        lv_obj_set_style_pad_all(row.button, 2, 0);
        lv_obj_set_width(row.label, LV_PCT(100));
        lv_label_set_long_mode(row.label, LV_LABEL_LONG_DOT);
        lv_obj_add_event_cb(row.button, row_clicked, LV_EVENT_CLICKED, &row);
        lv_obj_add_flag(row.button, LV_OBJ_FLAG_HIDDEN);
    }
    selection_label_ = lv_label_create(object_);
    lv_obj_set_width(selection_label_, LV_PCT(100));
    lv_label_set_long_mode(selection_label_, LV_LABEL_LONG_DOT);
    lv_label_set_text(selection_label_, "No track selected");
    use_selection_ = button(object_, "Use selection");
    lv_obj_add_event_cb(use_selection_, navigation_clicked, LV_EVENT_CLICKED, this);
    status_label_ = lv_label_create(object_);
    lv_obj_set_width(status_label_, LV_PCT(100));
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    refresh();
}

LvglDirectoryBrowser::~LvglDirectoryBrowser() {
    lv_obj_delete(object_);
}

void LvglDirectoryBrowser::set_selection_handler(SelectionHandler handler, void* context) noexcept {
    selection_handler_ = handler;
    selection_context_ = context;
    update_controls();
}

uint64_t LvglDirectoryBrowser::next_request_id() {
    if (request_counter_ == std::numeric_limits<uint64_t>::max()) {
        show_error(MEDIA_BROWSE_ERROR_RESOURCE_EXHAUSTED);
        return 0;
    }
    return ++request_counter_;
}

void LvglDirectoryBrowser::refresh() {
    if (!pending()) browse(requested_directory_);
}

void LvglDirectoryBrowser::browse(const sertial::fixed_string<512>& directory) {
    const auto id = next_request_id();
    if (id == 0) return;
    MediaDirectoryQuery query{.request_id = id, .directory = directory, .page_size = ROW_COUNT};
    if (worker_.submit(query) == media::DirectoryBrowserWorker::Submission::Busy) {
        lv_label_set_text(status_label_, "Browser busy; retry");
        return;
    }
    requested_directory_ = directory;
    pending_request_id_ = id;
    selected_row_ = ROW_COUNT;
    lv_label_set_text(selection_label_, "No track selected");
    lv_label_set_text(status_label_, "Reading directory...");
    update_controls();
}

void LvglDirectoryBrowser::request_page(uint32_t offset) {
    const auto id = next_request_id();
    if (id == 0) return;
    const MediaDirectoryPageRequest request{
        .request_id = id, .snapshot_id = page_.snapshot_id, .offset = offset, .page_size = ROW_COUNT};
    if (worker_.submit(request) == media::DirectoryBrowserWorker::Submission::Busy) {
        lv_label_set_text(status_label_, "Browser busy; retry");
        return;
    }
    pending_request_id_ = id;
    selected_row_ = ROW_COUNT;
    lv_label_set_text(selection_label_, "No track selected");
    lv_label_set_text(status_label_, "Reading page...");
    update_controls();
}

void LvglDirectoryBrowser::poll() {
    if (pending_selection_) {
        MediaSelectionResult selection{};
        if (!worker_.poll(selection)) return;
        pending_selection_ = false;
        const auto expected = pending_request_id_;
        pending_request_id_ = 0;
        if (selection.request_id != expected || selection.snapshot_id != page_.snapshot_id) {
            show_error(MEDIA_BROWSE_ERROR_STALE_SNAPSHOT);
        } else if (selection.error != MEDIA_BROWSE_ERROR_NONE) {
            show_error(selection.error);
        } else {
            const bool accepted = selection_handler_ != nullptr
                && selection_handler_(selection_context_, selection);
            lv_label_set_text(status_label_, accepted
                ? "Selection handed off; check load status" : "Selection rejected; retry");
            if (accepted) selected_row_ = ROW_COUNT;
        }
        update_controls();
        return;
    }
    MediaDirectoryPage result{};
    if (!worker_.poll(result)) return;
    if (result.request_id != pending_request_id_) {
        pending_request_id_ = 0;
        page_ = {};
        page_.error = MEDIA_BROWSE_ERROR_STALE_SNAPSHOT;
        show_page();
        return;
    }
    pending_request_id_ = 0;
    page_ = result;
    show_page();
}

void LvglDirectoryBrowser::validate_selection() {
    const auto id = next_request_id();
    if (id == 0) return;
    const MediaSelectionRequest request{
        .request_id = id, .snapshot_id = page_.snapshot_id,
        .relative_path = page_.entries[selected_row_].relative_path,
    };
    if (worker_.submit(request) == media::DirectoryBrowserWorker::Submission::Busy) {
        lv_label_set_text(status_label_, "Browser busy; retry");
        return;
    }
    pending_request_id_ = id;
    pending_selection_ = true;
    lv_label_set_text(status_label_, "Validating selected file...");
    update_controls();
}

void LvglDirectoryBrowser::show_error(MediaBrowseError error) {
    lv_label_set_text(status_label_, error_text(error));
}

void LvglDirectoryBrowser::show_page() {
    const bool valid = page_.error == MEDIA_BROWSE_ERROR_NONE && page_.snapshot_id != 0;
    const auto* directory_name = requested_directory_.empty() || requested_directory_ == "."
        ? "/" : requested_directory_ == "rekordbox://" ? "Rekordbox Library"
        : requested_directory_.c_str();
    lv_label_set_text(directory_label_, directory_name);
    for (uint32_t index = 0; index < ROW_COUNT; ++index) {
        auto& row = rows_[index];
        if (valid && index < page_.entries.size()) {
            lv_obj_remove_flag(row.button, LV_OBJ_FLAG_HIDDEN);
            const auto& entry = page_.entries[index];
            lv_label_set_text_fmt(row.label, "%s%s",
                entry.kind == MEDIA_DIRECTORY_ENTRY_REKORDBOX_LIBRARY ? "[Library] "
                    : entry.kind == MEDIA_DIRECTORY_ENTRY_DIRECTORY ? "[Folder] " : "",
                entry.name.c_str());
        } else {
            lv_obj_add_flag(row.button, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!valid) show_error(page_.error);
    else if (page_.entries.empty() && page_.skipped_entries != 0) {
        lv_label_set_text_fmt(status_label_, "No local tracks; %u skipped", page_.skipped_entries);
    } else if (page_.entries.empty()) lv_label_set_text(status_label_, "Empty directory");
    else lv_label_set_text_fmt(status_label_, "%u-%u of %u; %u skipped",
        page_.offset + 1, page_.next_offset, page_.total_entries, page_.skipped_entries);
    update_controls();
}

void LvglDirectoryBrowser::update_controls() {
    const bool valid = !pending() && page_.error == MEDIA_BROWSE_ERROR_NONE
        && page_.snapshot_id != 0;
    enabled(up_, !pending() && !requested_directory_.empty() && requested_directory_ != ".");
    enabled(refresh_, !pending());
    enabled(previous_, valid && page_.offset != 0);
    enabled(next_, valid && page_.has_more);
    enabled(use_selection_, valid && selected_row_ < page_.entries.size()
        && selection_handler_ != nullptr);
    for (auto& row : rows_) enabled(row.button, valid);
}

void LvglDirectoryBrowser::select(uint32_t index) {
    if (pending() || page_.error != MEDIA_BROWSE_ERROR_NONE || index >= page_.entries.size()) return;
    const auto& entry = page_.entries[index];
    if (entry.kind == MEDIA_DIRECTORY_ENTRY_DIRECTORY
        || entry.kind == MEDIA_DIRECTORY_ENTRY_REKORDBOX_LIBRARY) {
        browse(entry.relative_path);
    } else {
        selected_row_ = index;
        lv_label_set_text(selection_label_, entry.name.c_str());
        lv_label_set_text(status_label_, selection_handler_ == nullptr
            ? "Selection only; no load service connected" : "Track selected; use selection to load");
        update_controls();
    }
}

void LvglDirectoryBrowser::row_clicked(lv_event_t* event) {
    auto& row = *static_cast<Row*>(lv_event_get_user_data(event));
    row.owner->select(row.index);
}

void LvglDirectoryBrowser::navigation_clicked(lv_event_t* event) {
    auto& self = *static_cast<LvglDirectoryBrowser*>(lv_event_get_user_data(event));
    if (self.pending()) return;
    auto* target = lv_event_get_target_obj(event);
    if (lv_obj_has_state(target, LV_STATE_DISABLED)) return;
    if (target == self.refresh_) self.refresh();
    else if (target == self.next_) self.request_page(self.page_.next_offset);
    else if (target == self.previous_) self.request_page(self.page_.offset >= ROW_COUNT
        ? self.page_.offset - ROW_COUNT : 0);
    else if (target == self.up_) {
        std::string_view directory{self.requested_directory_.c_str()};
        const auto separator = directory.find_last_of('/');
        const auto parent = directory == "rekordbox://"
            ? std::string_view{}
            : separator == std::string_view::npos
                ? std::string_view{} : directory.substr(0, separator);
        self.browse(sertial::fixed_string<512>{parent});
    } else if (target == self.use_selection_ && self.selection_handler_ != nullptr
        && self.selected_row_ < self.page_.entries.size()) {
        self.validate_selection();
    }
}

} // namespace musicrat::backends
