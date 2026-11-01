#pragma once

#include <musicrat/protocol/media_library.hpp>
#include <musicrat/backends/media/file_identity.hpp>

#include <filesystem>
#include <vector>

namespace musicrat::backends::media {

class DirectoryBrowser {
public:
    [[nodiscard]] CommRaT::Messages::MediaBrowseError configure(
        const std::filesystem::path& root, uint32_t scan_limit = 4096);

    void browse(
        const CommRaT::Messages::MediaDirectoryQuery& query,
        CommRaT::Messages::MediaDirectoryPage& output);

    void read_page(
        const CommRaT::Messages::MediaDirectoryPageRequest& request,
        CommRaT::Messages::MediaDirectoryPage& output) const;

    void resolve_selection(
        const CommRaT::Messages::MediaSelectionRequest& request,
        CommRaT::Messages::MediaSelectionResult& output) const;

private:
    struct SnapshotEntry {
        CommRaT::Messages::MediaDirectoryEntry entry{};
        FileIdentity identity{};
    };

    [[nodiscard]] static CommRaT::Messages::MediaBrowseError identify_file(
        const std::filesystem::path& path, FileIdentity& identity);
    [[nodiscard]] CommRaT::Messages::MediaBrowseError resolve_directory(
        const std::filesystem::path& relative, std::filesystem::path& directory) const;
    [[nodiscard]] CommRaT::Messages::MediaBrowseError scan(
        const CommRaT::Messages::MediaDirectoryQuery& query);

    std::filesystem::path root_{};
    std::vector<SnapshotEntry> entries_{};
    uint64_t root_device_{0};
    uint64_t root_inode_{0};
    sertial::fixed_string<512> directory_{};
    uint32_t scan_limit_{4096};
    uint32_t skipped_entries_{0};
    uint64_t snapshot_counter_{0};
    uint64_t snapshot_id_{0};
};

} // namespace musicrat::backends::media
