#include <musicrat/backends/media/directory_browser.hpp>
#include <musicrat/backends/media/rekordbox_pdb.hpp>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/stat.h>

namespace musicrat::backends::media {

namespace {

using namespace CommRaT::Messages;
namespace fs = std::filesystem;
constexpr std::string_view kRekordboxLibraryDirectory{"rekordbox://"};

MediaBrowseError filesystem_error(const std::error_code& error) noexcept {
    if (error == std::errc::permission_denied) return MEDIA_BROWSE_ERROR_PERMISSION_DENIED;
    if (error == std::errc::no_such_file_or_directory) return MEDIA_BROWSE_ERROR_NOT_FOUND;
    if (error == std::errc::not_a_directory) return MEDIA_BROWSE_ERROR_NOT_DIRECTORY;
    if (error == std::errc::filename_too_long) return MEDIA_BROWSE_ERROR_PATH_TOO_LONG;
    return MEDIA_BROWSE_ERROR_IO;
}

bool valid_request(uint64_t request_id, uint32_t page_size) noexcept {
    return request_id != 0 && page_size != 0 && page_size <= MediaDirectoryPage::MAX_ENTRIES;
}

MediaBrowseError pdb_error(RekordboxPdbError error) noexcept {
    switch (error) {
    case RekordboxPdbError::None: return MEDIA_BROWSE_ERROR_NONE;
    case RekordboxPdbError::NotFound: return MEDIA_BROWSE_ERROR_NOT_FOUND;
    case RekordboxPdbError::Io: return MEDIA_BROWSE_ERROR_IO;
    case RekordboxPdbError::Malformed: return MEDIA_BROWSE_ERROR_INVALID_LIBRARY;
    case RekordboxPdbError::ResourceLimit: return MEDIA_BROWSE_ERROR_RESOURCE_EXHAUSTED;
    }
    return MEDIA_BROWSE_ERROR_INVALID_LIBRARY;
}

bool safe_relative_media_path(std::string path, std::string& relative) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0]))
        && path[1] == ':') {
        path.erase(0, 2);
        while (!path.empty() && path.front() == '/') path.erase(path.begin());
    }
    if (path.empty()) return false;
    const fs::path candidate{path};
    if (candidate.is_absolute() || candidate.has_root_name()) return false;
    for (const auto& component : candidate) {
        if (component == "..") return false;
    }
    const auto normalized = candidate.lexically_normal();
    relative = normalized.generic_string();
    return !relative.empty() && relative != ".";
}

bool export_pdb_present(const fs::path& root) {
    std::error_code error;
    auto current = root;
    for (const char* component : {"PIONEER", "rekordbox"}) {
        current /= component;
        const auto status = fs::symlink_status(current, error);
        if (error || fs::is_symlink(status) || !fs::is_directory(status)) return false;
    }
    current /= "export.pdb";
    const auto status = fs::symlink_status(current, error);
    return !error && !fs::is_symlink(status) && fs::is_regular_file(status);
}

std::string display_track_name(const RekordboxPdbTrack& track) {
    if (!track.artist.empty() && !track.title.empty()) {
        std::string combined = track.artist + " - " + track.title;
        if (combined.size() <= 255) return combined;
    }
    return track.title;
}

} // namespace

MediaBrowseError DirectoryBrowser::configure(const fs::path& root, uint32_t scan_limit) {
    root_.clear();
    entries_.clear();
    snapshot_id_ = 0;
    directory_.clear();
    skipped_entries_ = 0;
    if (root.empty() || root.native().find(fs::path::value_type{}) != fs::path::string_type::npos
        || scan_limit == 0 || scan_limit > 65536) {
        return MEDIA_BROWSE_ERROR_INVALID_REQUEST;
    }
    try {
        std::error_code error;
        auto canonical = fs::canonical(root, error);
        if (error) return filesystem_error(error);
        const auto status = fs::status(canonical, error);
        if (error) return filesystem_error(error);
        if (!fs::is_directory(status)) return MEDIA_BROWSE_ERROR_NOT_DIRECTORY;
        struct stat root_status{};
        if (::lstat(canonical.c_str(), &root_status) != 0) {
            return filesystem_error(std::error_code{errno, std::generic_category()});
        }
        if (!S_ISDIR(root_status.st_mode)) return MEDIA_BROWSE_ERROR_OUTSIDE_ROOT;
        root_device_ = root_status.st_dev;
        root_inode_ = root_status.st_ino;
        root_ = std::move(canonical);
        scan_limit_ = scan_limit;
        return MEDIA_BROWSE_ERROR_NONE;
    } catch (const std::bad_alloc&) {
        return MEDIA_BROWSE_ERROR_RESOURCE_EXHAUSTED;
    }
}

void DirectoryBrowser::browse(const MediaDirectoryQuery& query, MediaDirectoryPage& output) {
    output = {};
    output.request_id = query.request_id;
    snapshot_id_ = 0;
    entries_.clear();
    directory_.clear();
    skipped_entries_ = 0;
    if (!valid_request(query.request_id, query.page_size)) {
        output.error = MEDIA_BROWSE_ERROR_INVALID_REQUEST;
        return;
    }
    if (root_.empty()) {
        output.error = MEDIA_BROWSE_ERROR_NOT_CONFIGURED;
        return;
    }
    try {
        output.error = scan(query);
        if (output.error != MEDIA_BROWSE_ERROR_NONE) {
            entries_.clear();
            return;
        }
        if (snapshot_counter_ == std::numeric_limits<uint64_t>::max()) {
            output.error = MEDIA_BROWSE_ERROR_RESOURCE_EXHAUSTED;
            entries_.clear();
            return;
        }
        snapshot_id_ = ++snapshot_counter_;
        read_page({
            .request_id = query.request_id,
            .snapshot_id = snapshot_id_,
            .page_size = query.page_size,
        }, output);
    } catch (const std::bad_alloc&) {
        snapshot_id_ = 0;
        entries_.clear();
        output = {};
        output.request_id = query.request_id;
        output.error = MEDIA_BROWSE_ERROR_RESOURCE_EXHAUSTED;
    }
}

MediaBrowseError DirectoryBrowser::resolve_directory(
    const fs::path& relative, fs::path& directory) const {
    if (relative.is_absolute() || relative.has_root_name()) return MEDIA_BROWSE_ERROR_OUTSIDE_ROOT;
    const auto normalized = relative.lexically_normal();
    for (const auto& component : normalized) {
        if (component == "..") return MEDIA_BROWSE_ERROR_OUTSIDE_ROOT;
    }
    std::error_code error;
    const auto current_root = fs::canonical(root_, error);
    if (error) return filesystem_error(error);
    if (current_root != root_) return MEDIA_BROWSE_ERROR_OUTSIDE_ROOT;
    struct stat root_status{};
    if (::lstat(root_.c_str(), &root_status) != 0) {
        return filesystem_error(std::error_code{errno, std::generic_category()});
    }
    if (!S_ISDIR(root_status.st_mode) || root_status.st_dev != root_device_
        || root_status.st_ino != root_inode_) return MEDIA_BROWSE_ERROR_SELECTION_CHANGED;
    directory = root_;
    for (const auto& component : normalized) {
        if (component == ".") continue;
        directory /= component;
        const auto status = fs::symlink_status(directory, error);
        if (error) return filesystem_error(error);
        if (fs::is_symlink(status)) return MEDIA_BROWSE_ERROR_OUTSIDE_ROOT;
        if (!fs::is_directory(status)) return MEDIA_BROWSE_ERROR_NOT_DIRECTORY;
    }
    return MEDIA_BROWSE_ERROR_NONE;
}

MediaBrowseError DirectoryBrowser::identify_file(const fs::path& path, FileIdentity& identity) {
    struct stat status{};
    if (::lstat(path.c_str(), &status) != 0) {
        return filesystem_error(std::error_code{errno, std::generic_category()});
    }
    if (S_ISLNK(status.st_mode)) return MEDIA_BROWSE_ERROR_OUTSIDE_ROOT;
    if (!S_ISREG(status.st_mode)) return MEDIA_BROWSE_ERROR_NOT_FILE;
    identity = file_identity(status);
    return MEDIA_BROWSE_ERROR_NONE;
}

MediaBrowseError DirectoryBrowser::scan(const MediaDirectoryQuery& query) {
    const std::string_view requested{query.directory.c_str(), query.directory.size()};
    if (requested.find('\0') != std::string_view::npos) return MEDIA_BROWSE_ERROR_INVALID_REQUEST;
    if (requested == kRekordboxLibraryDirectory) {
        directory_ = kRekordboxLibraryDirectory.data();
        const auto catalog = read_rekordbox_pdb(root_);
        const auto catalog_error = pdb_error(catalog.error);
        if (catalog_error != MEDIA_BROWSE_ERROR_NONE) return catalog_error;
        for (const auto& track : catalog.tracks) {
            std::string relative;
            if (!safe_relative_media_path(track.file_path, relative)
                || relative.size() > MediaDirectoryEntry{}.relative_path.max_size()) {
                ++skipped_entries_;
                continue;
            }
            const fs::path relative_path{relative};
            fs::path parent;
            const auto directory_error = resolve_directory(relative_path.parent_path(), parent);
            if (directory_error != MEDIA_BROWSE_ERROR_NONE) {
                ++skipped_entries_;
                continue;
            }
            const auto absolute = parent / relative_path.filename();
            FileIdentity identity{};
            if (identify_file(absolute, identity) != MEDIA_BROWSE_ERROR_NONE) {
                ++skipped_entries_;
                continue;
            }
            auto display_name = display_track_name(track);
            if (display_name.empty()) display_name = relative_path.filename().string();
            if (display_name.empty() || display_name.size() > MediaDirectoryEntry{}.name.max_size()) {
                ++skipped_entries_;
                continue;
            }
            MediaDirectoryEntry entry{};
            entry.name = display_name.c_str();
            entry.relative_path = relative.c_str();
            entry.kind = MEDIA_DIRECTORY_ENTRY_FILE;
            entries_.push_back({.entry = entry, .identity = identity});
        }
        std::sort(entries_.begin(), entries_.end(), [](const auto& left, const auto& right) {
            if (left.entry.name != right.entry.name) return left.entry.name < right.entry.name;
            return left.entry.relative_path < right.entry.relative_path;
        });
        return MEDIA_BROWSE_ERROR_NONE;
    }

    fs::path directory;
    const auto resolved = resolve_directory(fs::path{requested}, directory);
    if (resolved != MEDIA_BROWSE_ERROR_NONE) return resolved;
    std::error_code error;
    const auto directory_name = directory.lexically_relative(root_).generic_string();
    if (directory_name.size() > directory_.max_size()) return MEDIA_BROWSE_ERROR_PATH_TOO_LONG;
    directory_ = directory_name.c_str();
    fs::directory_iterator iterator{directory, error};
    if (error) return filesystem_error(error);
    uint32_t scanned = 0;
    const fs::directory_iterator end;
    while (iterator != end) {
        if (scanned == scan_limit_) return MEDIA_BROWSE_ERROR_SCAN_LIMIT;
        ++scanned;
        const auto& entry = *iterator;
        const auto status = entry.symlink_status(error);
        if (error) return filesystem_error(error);
        if (fs::is_directory(status) || fs::is_regular_file(status)) {
            const auto name = entry.path().filename().string();
            const auto relative_name = entry.path().lexically_relative(root_).generic_string();
            MediaDirectoryEntry item{};
            if (name.size() > item.name.max_size()
                || relative_name.size() > item.relative_path.max_size()) {
                return MEDIA_BROWSE_ERROR_PATH_TOO_LONG;
            }
            item.name = name.c_str();
            item.relative_path = relative_name.c_str();
            item.kind = fs::is_directory(status)
                ? MEDIA_DIRECTORY_ENTRY_DIRECTORY : MEDIA_DIRECTORY_ENTRY_FILE;
            SnapshotEntry snapshot{.entry = item};
            if (item.kind == MEDIA_DIRECTORY_ENTRY_FILE) {
                const auto identified = identify_file(entry.path(), snapshot.identity);
                if (identified != MEDIA_BROWSE_ERROR_NONE) return identified;
            }
            entries_.push_back(snapshot);
        } else {
            ++skipped_entries_;
        }
        iterator.increment(error);
        if (error) return filesystem_error(error);
    }
    if (directory == root_ && export_pdb_present(root_)) {
        MediaDirectoryEntry entry{};
        entry.name = "Rekordbox Library";
        entry.relative_path = kRekordboxLibraryDirectory.data();
        entry.kind = MEDIA_DIRECTORY_ENTRY_REKORDBOX_LIBRARY;
        entries_.push_back({.entry = entry});
    }
    std::sort(entries_.begin(), entries_.end(), [](const auto& left, const auto& right) {
        if (left.entry.kind != right.entry.kind) return left.entry.kind < right.entry.kind;
        return std::string_view{left.entry.name.c_str()} < std::string_view{right.entry.name.c_str()};
    });
    return MEDIA_BROWSE_ERROR_NONE;
}

void DirectoryBrowser::read_page(
    const MediaDirectoryPageRequest& request, MediaDirectoryPage& output) const {
    output = {};
    output.request_id = request.request_id;
    if (!valid_request(request.request_id, request.page_size)) {
        output.error = MEDIA_BROWSE_ERROR_INVALID_REQUEST;
        return;
    }
    if (snapshot_id_ == 0 || request.snapshot_id != snapshot_id_) {
        output.error = MEDIA_BROWSE_ERROR_STALE_SNAPSHOT;
        return;
    }
    if (request.offset > entries_.size()) {
        output.error = MEDIA_BROWSE_ERROR_INVALID_REQUEST;
        return;
    }
    output.snapshot_id = snapshot_id_;
    output.directory = directory_;
    output.offset = request.offset;
    output.total_entries = static_cast<uint32_t>(entries_.size());
    output.skipped_entries = skipped_entries_;
    const auto count = std::min(request.page_size, output.total_entries - request.offset);
    for (uint32_t index = 0; index < count; ++index) {
        output.entries.push_back(entries_[request.offset + index].entry);
    }
    output.next_offset = request.offset + count;
    output.has_more = output.next_offset < output.total_entries;
}

void DirectoryBrowser::resolve_selection(
    const MediaSelectionRequest& request, MediaSelectionResult& output) const {
    output = {};
    output.request_id = request.request_id;
    output.snapshot_id = request.snapshot_id;
    const std::string_view relative{request.relative_path.c_str(), request.relative_path.size()};
    if (request.request_id == 0 || relative.empty() || relative.find('\0') != std::string_view::npos) {
        output.error = MEDIA_BROWSE_ERROR_INVALID_REQUEST;
        return;
    }
    if (snapshot_id_ == 0 || snapshot_id_ != request.snapshot_id) {
        output.error = MEDIA_BROWSE_ERROR_STALE_SNAPSHOT;
        return;
    }
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& item) {
        return item.entry.relative_path == request.relative_path;
    });
    if (found == entries_.end()) {
        output.error = MEDIA_BROWSE_ERROR_INVALID_REQUEST;
        return;
    }
    if (found->entry.kind != MEDIA_DIRECTORY_ENTRY_FILE) {
        output.error = MEDIA_BROWSE_ERROR_NOT_FILE;
        return;
    }
    try {
        const fs::path path{relative};
        fs::path directory;
        output.error = resolve_directory(path.parent_path(), directory);
        if (output.error != MEDIA_BROWSE_ERROR_NONE) return;
        const auto absolute = directory / path.filename();
        FileIdentity identity{};
        output.error = identify_file(absolute, identity);
        if (output.error != MEDIA_BROWSE_ERROR_NONE) return;
        if (identity != found->identity) {
            output.error = MEDIA_BROWSE_ERROR_SELECTION_CHANGED;
            return;
        }
        const auto resolved = absolute.string();
        if (resolved.size() > output.resolved_path.max_size()) {
            output.error = MEDIA_BROWSE_ERROR_PATH_TOO_LONG;
            return;
        }
        output.entry = found->entry;
        output.resolved_path = resolved.c_str();
    } catch (const std::bad_alloc&) {
        output.error = MEDIA_BROWSE_ERROR_RESOURCE_EXHAUSTED;
    }
}

} // namespace musicrat::backends::media
