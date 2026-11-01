#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace musicrat::backends::media {

struct RekordboxPdbTrack {
    uint32_t id{0};
    std::string title{};
    std::string artist{};
    std::string file_path{};
    uint32_t tempo_hundredths{0};
    uint32_t duration_seconds{0};
};

enum class RekordboxPdbError {
    None,
    NotFound,
    Io,
    Malformed,
    ResourceLimit,
};

struct RekordboxPdbResult {
    RekordboxPdbError error{RekordboxPdbError::NotFound};
    std::vector<RekordboxPdbTrack> tracks{};
};

// Reads track and artist rows from a classic export.pdb; no files are modified.
[[nodiscard]] RekordboxPdbResult read_rekordbox_pdb(
    const std::filesystem::path& usb_root);

} // namespace musicrat::backends::media
