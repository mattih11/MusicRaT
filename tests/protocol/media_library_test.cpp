#include <musicrat/protocol/media_library.hpp>

#include <sertial/message.hpp>

#include <cassert>
#include <string>

int main() {
    using namespace CommRaT::Messages;
    MediaDirectoryQuery query{.request_id = 17, .directory = "albums/live", .page_size = 16};
    const auto query_bytes = sertial::Message<MediaDirectoryQuery>::serialize(query);
    const auto restored_query = sertial::Message<MediaDirectoryQuery>::deserialize(query_bytes.view());
    assert(restored_query && restored_query->request_id == 17);
    assert(restored_query->directory == "albums/live" && restored_query->page_size == 16);

    MediaDirectoryPage page{};
    page.request_id = 19;
    page.snapshot_id = 9;
    page.directory = "albums/live";
    page.offset = 32;
    page.next_offset = 64;
    page.total_entries = 65;
    page.skipped_entries = 2;
    page.has_more = true;
    for (uint32_t index = 0; index < MediaDirectoryPage::MAX_ENTRIES; ++index) {
        MediaDirectoryEntry entry{};
        entry.name = ("track-" + std::to_string(index)).c_str();
        entry.relative_path = ("albums/live/track-" + std::to_string(index)).c_str();
        page.entries.push_back(entry);
    }
    const auto bytes = sertial::Message<MediaDirectoryPage>::serialize(page);
    const auto restored = sertial::Message<MediaDirectoryPage>::deserialize(bytes.view());
    assert(restored && restored->entries.size() == MediaDirectoryPage::MAX_ENTRIES);
    assert(restored->request_id == page.request_id && restored->snapshot_id == 9);
    assert(restored->directory == "albums/live");
    assert(restored->offset == 32 && restored->next_offset == 64);
    assert(restored->has_more && restored->total_entries == 65 && restored->skipped_entries == 2);
    assert(restored->entries.back().relative_path == "albums/live/track-31");

    page = {};
    page.error = MEDIA_BROWSE_ERROR_STALE_SNAPSHOT;
    const auto error_bytes = sertial::Message<MediaDirectoryPage>::serialize(page);
    const auto error = sertial::Message<MediaDirectoryPage>::deserialize(error_bytes.view());
    assert(error && error->error == MEDIA_BROWSE_ERROR_STALE_SNAPSHOT && error->entries.empty());

    MediaSelectionRequest selection{.request_id = 27, .snapshot_id = 12, .relative_path = "album/track.wav"};
    const auto selection_bytes = sertial::Message<MediaSelectionRequest>::serialize(selection);
    const auto restored_selection = sertial::Message<MediaSelectionRequest>::deserialize(selection_bytes.view());
    assert(restored_selection && restored_selection->request_id == 27 && restored_selection->snapshot_id == 12);
    assert(restored_selection->relative_path == selection.relative_path);
    MediaSelectionResult result{
        .request_id = 27, .snapshot_id = 12,
        .entry = {.name = "track.wav", .relative_path = "album/track.wav"},
        .resolved_path = "/media/album/track.wav",
    };
    const auto result_bytes = sertial::Message<MediaSelectionResult>::serialize(result);
    const auto restored_result = sertial::Message<MediaSelectionResult>::deserialize(result_bytes.view());
    assert(restored_result && restored_result->resolved_path == result.resolved_path);
    assert(restored_result->entry.relative_path == selection.relative_path);
    assert(restored_result->request_id == 27 && restored_result->snapshot_id == 12);
    result = {.request_id = 28, .snapshot_id = 12, .error = MEDIA_BROWSE_ERROR_SELECTION_CHANGED};
    const auto failed_bytes = sertial::Message<MediaSelectionResult>::serialize(result);
    const auto failed = sertial::Message<MediaSelectionResult>::deserialize(failed_bytes.view());
    assert(failed && failed->error == MEDIA_BROWSE_ERROR_SELECTION_CHANGED && failed->resolved_path.empty());
}
