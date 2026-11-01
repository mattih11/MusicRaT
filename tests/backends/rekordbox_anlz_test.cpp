#include <musicrat/backends/media/rekordbox_anlz.hpp>
#include <musicrat/dsp/beat_grid.hpp>

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using musicrat::backends::media::RekordboxImportError;

void put16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value >> 8U);
    bytes[offset + 1] = static_cast<uint8_t>(value);
}

void put32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value >> 24U);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 16U);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 8U);
    bytes[offset + 3] = static_cast<uint8_t>(value);
}

void tag(std::vector<uint8_t>& bytes, std::size_t offset, const char* text) {
    for (std::size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(text[i]);
}

std::vector<uint8_t> ppth(const std::string& path) {
    std::vector<uint8_t> section(16 + (path.size() + 1) * 2);
    tag(section, 0, "PPTH");
    put32(section, 4, 16);
    put32(section, 8, static_cast<uint32_t>(section.size()));
    put32(section, 12, 1);
    put32(section, 12, static_cast<uint32_t>((path.size() + 1) * 2));
    for (std::size_t i = 0; i < path.size(); ++i) {
        section[16 + i * 2 + 1] = static_cast<uint8_t>(path[i]);
    }
    return section;
}

std::vector<uint8_t> pqtz() {
    constexpr uint32_t count = 6;
    std::vector<uint8_t> section(24 + count * 8);
    tag(section, 0, "PQTZ");
    put32(section, 4, 24);
    put32(section, 8, static_cast<uint32_t>(section.size()));
    put32(section, 20, count);
    const uint16_t tempos[count]{12000, 12000, 12000, 12800, 12800, 12800};
    const uint32_t times[count]{1000, 1500, 2000, 2500, 2969, 3438};
    for (uint32_t i = 0; i < count; ++i) {
        const auto offset = 24 + i * 8;
        put16(section, offset, static_cast<uint16_t>(i % 4 + 1));
        put16(section, offset + 2, tempos[i]);
        put32(section, offset + 4, times[i]);
    }
    return section;
}

std::vector<uint8_t> pcob() {
    std::vector<uint8_t> section(24);
    tag(section, 0, "PCOB");
    put32(section, 4, 24);
    put32(section, 8, 24);
    return section;
}

std::vector<uint8_t> pcob_with_cue() {
    constexpr std::size_t entry_size = 56;
    std::vector<uint8_t> section(24 + entry_size);
    tag(section, 0, "PCOB");
    put32(section, 4, 24);
    put32(section, 8, static_cast<uint32_t>(section.size()));
    put32(section, 12, 1);
    put16(section, 18, 1);
    tag(section, 24, "PCPT");
    put32(section, 28, 12);
    put32(section, 32, entry_size);
    put32(section, 36, 0);
    put32(section, 56, 1500);
    return section;
}

std::vector<uint8_t> pco2() {
    constexpr std::size_t entry_size = 32;
    std::vector<uint8_t> section(20 + 2 * entry_size);
    tag(section, 0, "PCO2");
    put32(section, 4, 20);
    put32(section, 8, static_cast<uint32_t>(section.size()));
    put32(section, 12, 1);
    put16(section, 16, 2);
    for (uint32_t i = 0; i < 2; ++i) {
        const auto entry = 20 + i * entry_size;
        tag(section, entry, "PCP2");
        put32(section, entry + 4, 12);
        put32(section, entry + 8, entry_size);
        put32(section, entry + 12, i);
        put32(section, entry + 20, i == 0 ? 2000 : 3000);
        put32(section, entry + 24, i == 0 ? 0 : 5000);
    }
    return section;
}

std::vector<uint8_t> pco2_empty() {
    std::vector<uint8_t> section(20);
    tag(section, 0, "PCO2");
    put32(section, 4, 20);
    put32(section, 8, 20);
    return section;
}

std::vector<uint8_t> anlz(std::vector<uint8_t> path_section,
                          std::vector<uint8_t> grid_section,
                          std::vector<uint8_t> cue_section) {
    std::vector<uint8_t> bytes(28);
    tag(bytes, 0, "PMAI");
    put32(bytes, 4, 28);
    bytes.insert(bytes.end(), path_section.begin(), path_section.end());
    if (!grid_section.empty()) bytes.insert(bytes.end(), grid_section.begin(), grid_section.end());
    if (!cue_section.empty()) bytes.insert(bytes.end(), cue_section.begin(), cue_section.end());
    put32(bytes, 8, static_cast<uint32_t>(bytes.size()));
    return bytes;
}

void write(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    assert(output.good());
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string{argv[1]} == "--usb-track") {
        const auto result = musicrat::backends::media::import_rekordbox_anlz(
            argv[2], argv[3], 48000, 1'000'000'000);
        std::printf("ANLZ status=%u grid=%u cues=%u\n",
            static_cast<unsigned>(result.error),
            result.metadata.grid_set ? 1U : 0U,
            result.metadata.cue_count);
        return result.error == RekordboxImportError::None ? 0 : 1;
    }

    const auto root = fs::temp_directory_path() / "musicrat-rekordbox-anlz-test";
    fs::remove_all(root);
    const auto media = root / "Contents" / "Music" / "song.wav";
    fs::create_directories(media.parent_path());
    std::ofstream(media).put('\0');
    const auto anlz_dir = root / "PIONEER" / "USBANLZ" / "P001" / "00000001";
    const auto path_section = ppth("/Contents/Music/song.wav");
    write(anlz_dir / "ANLZ0000.DAT",
        anlz(path_section, pqtz(), pcob()));
    write(anlz_dir / "ANLZ0000.EXT",
        anlz(path_section, {}, pco2()));

    const auto imported = musicrat::backends::media::import_rekordbox_anlz(
        root, media, 48000, 480000);
    assert(imported.error == RekordboxImportError::None);
    assert(imported.metadata.grid_set);
    assert(imported.metadata.beat_grid.flags & CommRaT::Messages::BEAT_GRID_FLAG_IMPORTED);
    assert(imported.metadata.beat_grid.segments.size() == 2);
    assert(imported.metadata.beat_grid.segments[0].start_frame == 48000);
    assert(imported.metadata.beat_grid.segments[1].tempo_bpm == 128.0);
    assert(musicrat::dsp::BeatGridMapper{imported.metadata.beat_grid}.valid());
    assert(imported.metadata.cue_count == 2);
    assert(imported.metadata.cues[0].frame == 96000);
    assert(imported.metadata.cues[0].hot && imported.metadata.cues[0].hot_cue == 0);
    assert(imported.metadata.markers.loop_start_set && imported.metadata.markers.loop_end_set);
    assert(imported.metadata.markers.loop_start_frame == 144000);
    assert(imported.metadata.markers.loop_end_frame == 240000);

    write(anlz_dir / "ANLZ0000.DAT", anlz(path_section, pqtz(), pcob_with_cue()));
    write(anlz_dir / "ANLZ0000.EXT", anlz(path_section, {}, pco2_empty()));
    const auto legacy_cue = musicrat::backends::media::import_rekordbox_anlz(
        root, media, 48000, 480000);
    assert(legacy_cue.error == RekordboxImportError::None);
    assert(legacy_cue.metadata.cue_count == 1);
    assert(legacy_cue.metadata.cues[0].frame == 72000);
    assert(legacy_cue.metadata.cues[0].hot);

    const auto other = root / "Contents" / "Music" / "other.wav";
    std::ofstream(other).put('\0');
    assert(musicrat::backends::media::import_rekordbox_anlz(
        root, other, 48000, 480000).error == RekordboxImportError::NotFound);
    assert(musicrat::backends::media::import_rekordbox_anlz(
        root, root.parent_path() / "outside.wav", 48000, 480000).error
        == RekordboxImportError::InvalidRequest);

    auto malformed = anlz(path_section, pqtz(), {});
    put32(malformed, 28 + path_section.size() + 8, 0xffffffffU);
    write(anlz_dir / "ANLZ0000.DAT", malformed);
    assert(musicrat::backends::media::import_rekordbox_anlz(
        root, media, 48000, 480000).error == RekordboxImportError::Malformed);
    fs::remove_all(root);
}
