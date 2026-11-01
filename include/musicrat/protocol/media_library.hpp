#pragma once

#include <sertial/containers/fixed_string.hpp>
#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

using MediaBrowseError = uint8_t;

inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_NONE = 0;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_INVALID_REQUEST = 1;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_NOT_CONFIGURED = 2;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_NOT_FOUND = 3;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_NOT_DIRECTORY = 4;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_OUTSIDE_ROOT = 5;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_PERMISSION_DENIED = 6;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_IO = 7;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_PATH_TOO_LONG = 8;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_SCAN_LIMIT = 9;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_STALE_SNAPSHOT = 10;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_RESOURCE_EXHAUSTED = 11;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_SELECTION_CHANGED = 12;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_NOT_FILE = 13;
inline constexpr MediaBrowseError MEDIA_BROWSE_ERROR_INVALID_LIBRARY = 14;

using MediaDirectoryEntryKind = uint8_t;

inline constexpr MediaDirectoryEntryKind MEDIA_DIRECTORY_ENTRY_DIRECTORY = 0;
inline constexpr MediaDirectoryEntryKind MEDIA_DIRECTORY_ENTRY_FILE = 1;
inline constexpr MediaDirectoryEntryKind MEDIA_DIRECTORY_ENTRY_REKORDBOX_LIBRARY = 2;

inline constexpr uint32_t MEDIA_DIRECTORY_PAGE_CAPACITY = 32;

struct MediaDirectoryEntry {
    sertial::fixed_string<255> name{};
    sertial::fixed_string<512> relative_path{};
    MediaDirectoryEntryKind kind{MEDIA_DIRECTORY_ENTRY_FILE};
};

struct MediaDirectoryQuery {
    uint64_t request_id{0};
    sertial::fixed_string<512> directory{};
    uint32_t page_size{MEDIA_DIRECTORY_PAGE_CAPACITY};
};

struct MediaDirectoryPageRequest {
    uint64_t request_id{0};
    uint64_t snapshot_id{0};
    uint32_t offset{0};
    uint32_t page_size{MEDIA_DIRECTORY_PAGE_CAPACITY};
};

struct MediaDirectoryPage {
    static constexpr std::size_t MAX_ENTRIES = MEDIA_DIRECTORY_PAGE_CAPACITY;

    uint64_t request_id{0};
    uint64_t snapshot_id{0};
    sertial::fixed_string<512> directory{};
    uint32_t offset{0};
    uint32_t next_offset{0};
    uint32_t total_entries{0};
    uint32_t skipped_entries{0};
    MediaBrowseError error{MEDIA_BROWSE_ERROR_NONE};
    bool has_more{false};
    sertial::fixed_vector<MediaDirectoryEntry, MAX_ENTRIES> entries{};
};

struct MediaSelectionRequest {
    uint64_t request_id{0};
    uint64_t snapshot_id{0};
    sertial::fixed_string<512> relative_path{};
};

struct MediaSelectionResult {
    uint64_t request_id{0};
    uint64_t snapshot_id{0};
    MediaBrowseError error{MEDIA_BROWSE_ERROR_NONE};
    MediaDirectoryEntry entry{};
    sertial::fixed_string<512> resolved_path{};
};

} // namespace CommRaT::Messages
