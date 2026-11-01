#pragma once

#include <cstdint>
#include <optional>
#include <sys/stat.h>

namespace musicrat::backends::media {

struct FileIdentity {
    uint64_t device{0}, inode{0};
    int64_t size{0}, modified_seconds{0}, modified_nanoseconds{0};
    int64_t changed_seconds{0}, changed_nanoseconds{0};
    bool operator==(const FileIdentity&) const = default;
};

inline FileIdentity file_identity(const struct stat& status) noexcept {
    return {.device = status.st_dev, .inode = status.st_ino, .size = status.st_size,
        .modified_seconds = status.st_mtim.tv_sec, .modified_nanoseconds = status.st_mtim.tv_nsec,
        .changed_seconds = status.st_ctim.tv_sec, .changed_nanoseconds = status.st_ctim.tv_nsec};
}

inline std::optional<FileIdentity> regular_file_identity(const char* path) noexcept {
    struct stat status{};
    if (::lstat(path, &status) != 0 || !S_ISREG(status.st_mode)) return std::nullopt;
    return file_identity(status);
}

} // namespace musicrat::backends::media
