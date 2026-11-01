#pragma once

#include <musicrat/protocol/deck_control.hpp>
#include <musicrat/protocol/transport.hpp>

#include <array>
#include <cstdint>
#include <filesystem>

namespace musicrat::backends::media {

struct RekordboxCuePoint {
    uint64_t frame{0};
    uint8_t hot_cue{0};
    bool hot{false};
    bool loop{false};
    bool operator==(const RekordboxCuePoint&) const = default;
};

struct RekordboxTrackMetadata {
    CommRaT::Messages::BeatGrid beat_grid{};
    CommRaT::Messages::DeckMarkerState markers{};
    std::array<RekordboxCuePoint, 8> cues{};
    uint32_t cue_count{0};
    bool grid_set{false};
    bool cues_set{false};
    bool operator==(const RekordboxTrackMetadata&) const = default;
};

enum class RekordboxImportError {
    None,
    InvalidRequest,
    NotFound,
    Io,
    Malformed,
    ResourceLimit,
};

struct RekordboxImportResult {
    RekordboxImportError error{RekordboxImportError::NotFound};
    RekordboxTrackMetadata metadata{};
};

// Reads classic Rekordbox ANLZ files below PIONEER/USBANLZ. No files are modified.
[[nodiscard]] RekordboxImportResult import_rekordbox_anlz(
    const std::filesystem::path& usb_root,
    const std::filesystem::path& media_path,
    uint32_t sample_rate_hz,
    uint64_t duration_frames);

} // namespace musicrat::backends::media
