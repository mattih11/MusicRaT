#include <musicrat/backends/media/rekordbox_pdb.hpp>
#include <musicrat/backends/media/directory_browser.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using musicrat::backends::media::RekordboxPdbError;

void put16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
}

void put32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
    for (uint32_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
}

void short_string(std::vector<uint8_t>& page, std::size_t row, uint16_t offset,
                  const std::string& value) {
    assert(value.size() <= 126);
    const auto at = row + offset;
    page[at] = static_cast<uint8_t>((value.size() + 1U) * 2U + 1U);
    for (std::size_t i = 0; i < value.size(); ++i) page[at + 1 + i] = static_cast<uint8_t>(value[i]);
}

void utf16_string(std::vector<uint8_t>& page, std::size_t row, uint16_t offset,
                  const std::vector<uint16_t>& value) {
    const auto at = row + offset;
    page[at] = 0x90;
    put16(page, at + 1, static_cast<uint16_t>(value.size() * 2 + 4));
    page[at + 3] = 0;
    for (std::size_t i = 0; i < value.size(); ++i) put16(page, at + 4 + i * 2, value[i]);
}

void set_page(std::vector<uint8_t>& bytes, uint32_t page_index, uint32_t page_type,
              uint32_t next_page, uint8_t flags, uint16_t row_offset = 0) {
    constexpr std::size_t page_size = 4096;
    const auto page = static_cast<std::size_t>(page_index) * page_size;
    put32(bytes, page + 4, page_index);
    put32(bytes, page + 8, page_type);
    put32(bytes, page + 12, next_page);
    if ((flags & 0x40U) == 0) {
        put16(bytes, page + 24, 1);
        bytes[page + 26] = 0x20;
        put16(bytes, page + page_size - 4, 1);
        put16(bytes, page + page_size - 6, row_offset);
    }
    bytes[page + 27] = flags;
}

std::vector<uint8_t> make_pdb(const std::string& media_path = "/Contents/Music/song.wav") {
    constexpr std::size_t page_size = 4096;
    std::vector<uint8_t> bytes(page_size * 4);
    put32(bytes, 4, page_size);
    put32(bytes, 8, 2);
    put32(bytes, 12, 4);
    put32(bytes, 28, 0);
    put32(bytes, 36, 1);
    put32(bytes, 40, 2);
    put32(bytes, 44, 2);
    put32(bytes, 52, 3);
    put32(bytes, 56, 3);

    set_page(bytes, 1, 0, 2, 0x64);
    set_page(bytes, 2, 0, 2, 0x24, 64);
    set_page(bytes, 3, 2, 3, 0x24, 64);

    constexpr std::size_t row = page_size * 2 + 40 + 64;
    put16(bytes, row, 0x24);
    put32(bytes, row + 68, 42);
    put32(bytes, row + 72, 1234);
    put32(bytes, row + 56, 12800);
    put16(bytes, row + 84, 191);
    put16(bytes, row + 94 + 17 * 2, 136);
    put16(bytes, row + 94 + 19 * 2, 180);
    put16(bytes, row + 94 + 20 * 2, 194);
    utf16_string(bytes, row, 136, {0x0043, 0x0061, 0x0066, 0x00e9});
    short_string(bytes, row, 180, "song.wav");
    short_string(bytes, row, 194, media_path);

    constexpr std::size_t artist = page_size * 3 + 40 + 64;
    put16(bytes, artist, 0x60);
    put32(bytes, artist + 4, 42);
    bytes[artist + 9] = 10;
    short_string(bytes, artist, 10, "An Artist");
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
    if (argc == 3 && std::string{argv[1]} == "--usb-root") {
        const auto result = musicrat::backends::media::read_rekordbox_pdb(argv[2]);
        std::printf("PDB status=%u tracks=%zu\n",
            static_cast<unsigned>(result.error), result.tracks.size());
        return result.error == RekordboxPdbError::None ? 0 : 1;
    }
    if (argc == 3 && std::string{argv[1]} == "--usb-library") {
        musicrat::backends::media::DirectoryBrowser browser;
        const auto configure_error = browser.configure(argv[2]);
        CommRaT::Messages::MediaDirectoryPage page{};
        if (configure_error == CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE) {
            browser.browse({.request_id = 1, .directory = "rekordbox://"}, page);
        } else {
            page.error = configure_error;
        }
        CommRaT::Messages::MediaSelectionResult selection{};
        if (page.error == CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE
            && !page.entries.empty()) {
            browser.resolve_selection({.request_id = 2, .snapshot_id = page.snapshot_id,
                .relative_path = page.entries[0].relative_path}, selection);
        }
        std::printf("PDB browser status=%u visible=%u skipped=%u\n",
            static_cast<unsigned>(page.error), page.total_entries, page.skipped_entries);
        std::printf("PDB selection status=%u\n", static_cast<unsigned>(selection.error));
        return page.error == CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE
            && (page.total_entries == 0
                || selection.error == CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE) ? 0 : 1;
    }

    const auto root = fs::temp_directory_path() / "musicrat-rekordbox-pdb-test";
    fs::remove_all(root);
    const auto pdb = root / "PIONEER" / "rekordbox" / "export.pdb";
    write(pdb, make_pdb());
    const auto media = root / "Contents" / "Music" / "song.wav";
    fs::create_directories(media.parent_path());
    std::ofstream(media).put('\0');

    const auto result = musicrat::backends::media::read_rekordbox_pdb(root);
    assert(result.error == RekordboxPdbError::None);
    assert(result.tracks.size() == 1);
    assert(result.tracks[0].id == 1234);
    assert(result.tracks[0].title == "Caf\xc3\xa9");
    assert(result.tracks[0].artist == "An Artist");
    assert(result.tracks[0].file_path == "/Contents/Music/song.wav");
    assert(result.tracks[0].tempo_hundredths == 12800);
    assert(result.tracks[0].duration_seconds == 191);

    musicrat::backends::media::DirectoryBrowser browser;
    assert(browser.configure(root) == CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE);
    CommRaT::Messages::MediaDirectoryPage page{};
    browser.browse({.request_id = 1}, page);
    assert(page.error == CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE);
    assert(std::any_of(page.entries.begin(), page.entries.end(), [](const auto& entry) {
        return entry.name == "Rekordbox Library" && entry.relative_path == "rekordbox://"
            && entry.kind == CommRaT::Messages::MEDIA_DIRECTORY_ENTRY_REKORDBOX_LIBRARY;
    }));
    browser.browse({.request_id = 2, .directory = "rekordbox://"}, page);
    assert(page.error == CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE);
    assert(page.total_entries == 1 && page.entries[0].name == "An Artist - Caf\xc3\xa9");
    CommRaT::Messages::MediaSelectionResult selection{};
    browser.resolve_selection({.request_id = 3, .snapshot_id = page.snapshot_id,
        .relative_path = page.entries[0].relative_path}, selection);
    assert(selection.error == CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE);
    assert(selection.resolved_path == media.string().c_str());

    write(pdb, make_pdb("../../outside.wav"));
    browser.browse({.request_id = 4, .directory = "rekordbox://"}, page);
    assert(page.error == CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE);
    assert(page.total_entries == 0 && page.skipped_entries == 1);

    auto malformed = make_pdb();
    put32(malformed, 4, 3);
    write(pdb, malformed);
    assert(musicrat::backends::media::read_rekordbox_pdb(root).error
        == RekordboxPdbError::Malformed);
    fs::remove_all(root);
}
