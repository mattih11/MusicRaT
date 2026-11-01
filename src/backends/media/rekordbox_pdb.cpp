#include <musicrat/backends/media/rekordbox_pdb.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>

namespace musicrat::backends::media {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxDatabaseBytes = 128U * 1024U * 1024U;
constexpr uint32_t kMaxPageSize = 65536;
constexpr uint32_t kMaxTables = 64;
constexpr uint32_t kMaxTracks = 100000;
constexpr uint32_t kMaxArtists = 65536;
constexpr uint32_t kMaxRowsPerTable = 200000;
constexpr std::size_t kDatabaseHeaderBytes = 28;
constexpr std::size_t kTableRecordBytes = 16;
constexpr std::size_t kPageHeaderBytes = 40;
constexpr std::size_t kTrackFixedBytes = 136;

uint16_t u16(const std::vector<uint8_t>& bytes, std::size_t offset) {
    return static_cast<uint16_t>(bytes[offset] | (static_cast<uint16_t>(bytes[offset + 1]) << 8U));
}

uint32_t u32(const std::vector<uint8_t>& bytes, std::size_t offset) {
    return static_cast<uint32_t>(bytes[offset])
        | (static_cast<uint32_t>(bytes[offset + 1]) << 8U)
        | (static_cast<uint32_t>(bytes[offset + 2]) << 16U)
        | (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

bool fits(std::size_t offset, std::size_t count, std::size_t length) {
    return offset <= length && count <= length - offset;
}

bool append_utf8(std::string& output, uint32_t cp) {
    if (cp <= 0x7fU) {
        output.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (cp >> 6U)));
        output.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    } else if (cp <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (cp >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    } else if (cp <= 0x10ffffU) {
        output.push_back(static_cast<char>(0xf0U | (cp >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    } else {
        return false;
    }
    return true;
}

bool decode_utf16le(const std::vector<uint8_t>& bytes, std::size_t offset,
                    std::size_t length, std::string& output) {
    if ((length & 1U) != 0 || !fits(offset, length, bytes.size())) return false;
    output.clear();
    for (std::size_t i = 0; i < length; i += 2) {
        uint32_t cp = static_cast<uint32_t>(bytes[offset + i])
            | (static_cast<uint32_t>(bytes[offset + i + 1]) << 8U);
        if (cp >= 0xd800U && cp <= 0xdbffU) {
            if (i + 3 >= length) return false;
            const uint32_t low = static_cast<uint32_t>(bytes[offset + i + 2])
                | (static_cast<uint32_t>(bytes[offset + i + 3]) << 8U);
            if (low < 0xdc00U || low > 0xdfffU) return false;
            cp = 0x10000U + ((cp - 0xd800U) << 10U) + low - 0xdc00U;
            i += 2;
        } else if (cp >= 0xdc00U && cp <= 0xdfffU) {
            return false;
        }
        if (cp != 0 && !append_utf8(output, cp)) return false;
    }
    return true;
}

bool decode_sql_string(const std::vector<uint8_t>& bytes, std::size_t row_base,
                       std::size_t row_limit, uint16_t relative_offset, std::string& output) {
    output.clear();
    if (relative_offset == 0) return true;
    if (relative_offset >= row_limit || !fits(row_base, row_limit, bytes.size())) return false;
    const auto at = row_base + relative_offset;
    if (at >= row_base + row_limit) return false;
    const auto kind = bytes[at];
    if (kind == 0) return true;
    if (kind == 0x40 || kind == 0x90) {
        if (!fits(at, 4, row_base + row_limit)) return false;
        const auto string_length = static_cast<std::size_t>(u16(bytes, at + 1));
        if (string_length < 4 || !fits(at, string_length + 1, row_base + row_limit)) return false;
        const auto text_length = string_length - 4;
        if (kind == 0x90) return decode_utf16le(bytes, at + 4, text_length, output);
        for (std::size_t i = 0; i < text_length; ++i) {
            if (!append_utf8(output, bytes[at + 4 + i])) return false;
        }
        return true;
    }

    const auto total_length = static_cast<std::size_t>(kind >> 1U);
    if (total_length == 0 || !fits(at, total_length, row_base + row_limit)) return false;
    const auto text_length = total_length - 1;
    for (std::size_t i = 0; i < text_length; ++i) {
        if (!append_utf8(output, bytes[at + 1 + i])) return false;
    }
    return true;
}

struct Table {
    uint32_t type{0};
    uint32_t first_page{0};
    uint32_t last_page{0};
};

struct Database {
    uint32_t page_size{0};
    uint32_t page_count{0};
    std::vector<Table> tables{};
};

bool read_database_header(const std::vector<uint8_t>& bytes, Database& database) {
    if (bytes.size() < kDatabaseHeaderBytes) return false;
    const auto page_size = u32(bytes, 4);
    const auto table_count = u32(bytes, 8);
    if (page_size < kDatabaseHeaderBytes || page_size > kMaxPageSize
        || (page_size & (page_size - 1U)) != 0 || table_count == 0 || table_count > kMaxTables
        || !fits(kDatabaseHeaderBytes, static_cast<std::size_t>(table_count) * kTableRecordBytes,
                 page_size)
        || bytes.size() % page_size != 0) return false;
    const auto page_count = bytes.size() / page_size;
    if (page_count == 0 || page_count > std::numeric_limits<uint32_t>::max()) return false;
    database.page_size = page_size;
    database.page_count = static_cast<uint32_t>(page_count);
    database.tables.clear();
    database.tables.reserve(table_count);
    for (uint32_t i = 0; i < table_count; ++i) {
        const auto offset = kDatabaseHeaderBytes + static_cast<std::size_t>(i) * kTableRecordBytes;
        Table table{.type = u32(bytes, offset), .first_page = u32(bytes, offset + 8),
                    .last_page = u32(bytes, offset + 12)};
        if (table.first_page >= database.page_count || table.last_page >= database.page_count) {
            return false;
        }
        database.tables.push_back(table);
    }
    return true;
}

bool find_table(const Database& database, uint32_t type, Table& table, bool required) {
    bool found = false;
    for (const auto& candidate : database.tables) {
        if (candidate.type != type) continue;
        if (found) return false;
        table = candidate;
        found = true;
    }
    return found || !required;
}

template<typename Handler>
bool for_each_row(const std::vector<uint8_t>& bytes, const Database& database,
                  const Table& table, uint32_t max_rows, Handler&& handler) {
    std::vector<uint8_t> visited(database.page_count, 0);
    uint32_t page_index = table.first_page;
    uint32_t row_count = 0;
    for (uint32_t page_visits = 0; page_visits < database.page_count; ++page_visits) {
        if (page_index >= database.page_count || visited[page_index] != 0) return false;
        visited[page_index] = 1;
        const auto page = static_cast<std::size_t>(page_index) * database.page_size;
        if (!fits(page, database.page_size, bytes.size())
            || u32(bytes, page + 4) != page_index || u32(bytes, page + 8) != table.type) return false;
        const auto next_page = u32(bytes, page + 12);
        const auto packed_counts = static_cast<uint32_t>(bytes[page + 24])
            | (static_cast<uint32_t>(bytes[page + 25]) << 8U)
            | (static_cast<uint32_t>(bytes[page + 26]) << 16U);
        const auto allocated_rows = packed_counts & 0x1fffU;
        const auto page_flags = bytes[page + 27];
        if ((page_flags & 0x40U) == 0 && allocated_rows != 0) {
            const uint32_t groups = (allocated_rows + 15U) / 16U;
            for (uint32_t group = 0; group < groups; ++group) {
                const auto base = static_cast<std::size_t>(database.page_size)
                    - static_cast<std::size_t>(group) * 0x24U;
                if (base < kPageHeaderBytes + 6U + 0x20U) return false;
                const auto present_flags = u16(bytes, page + base - 4U);
                const auto slots = std::min(16U, allocated_rows - group * 16U);
                for (uint32_t row_index = 0; row_index < slots; ++row_index) {
                    if (((present_flags >> row_index) & 1U) == 0) continue;
                    const auto row_offset_position = page + base - 6U - 2U * row_index;
                    if (!fits(row_offset_position, 2, page + database.page_size)) return false;
                    const auto row_offset = static_cast<std::size_t>(u16(bytes, row_offset_position));
                    const auto row_base = page + kPageHeaderBytes + row_offset;
                    const auto heap_limit = page + database.page_size
                        - static_cast<std::size_t>(groups) * 0x24U;
                    if (row_base >= heap_limit) return false;
                    if (++row_count > max_rows || !handler(row_base, page)) return false;
                }
            }
        }
        if (page_index == table.last_page) return true;
        if (next_page >= database.page_count || next_page == page_index) return false;
        page_index = next_page;
    }
    return false;
}

bool read_artist_rows(const std::vector<uint8_t>& bytes, const Database& database,
                      std::unordered_map<uint32_t, std::string>& artists) {
    Table table{};
    if (!find_table(database, 2, table, false)) return false;
    if (table.first_page == 0 && table.last_page == 0) return true;
    return for_each_row(bytes, database, table, kMaxArtists,
        [&](std::size_t row, std::size_t page) {
            if (!fits(row, 10, page + database.page_size)) return false;
            const auto subtype = u16(bytes, row);
            const auto row_id = u32(bytes, row + 4);
            uint16_t string_offset = bytes[row + 9];
            if ((subtype & 0x04U) != 0) {
                if (!fits(row, 12, page + database.page_size)) return false;
                string_offset = u16(bytes, row + 10);
            }
            if (row_id == 0 || string_offset == 0 || row + string_offset >= page + database.page_size) {
                return true;
            }
            std::string name;
            if (!decode_sql_string(bytes, row, page + database.page_size - row,
                                   string_offset, name)) return false;
            if (!name.empty()) {
                if (artists.size() >= kMaxArtists && !artists.contains(row_id)) return false;
                artists.insert_or_assign(row_id, std::move(name));
            }
            return true;
        });
}

bool read_track_rows(const std::vector<uint8_t>& bytes, const Database& database,
                     const std::unordered_map<uint32_t, std::string>& artists,
                     std::vector<RekordboxPdbTrack>& tracks) {
    Table table{};
    if (!find_table(database, 0, table, true)) return false;
    std::unordered_map<uint32_t, bool> ids;
    return for_each_row(bytes, database, table, kMaxRowsPerTable,
        [&](std::size_t row, std::size_t page) {
            if (!fits(row, kTrackFixedBytes, page + database.page_size)) return false;
            const auto track_id = u32(bytes, row + 72);
            const auto artist_id = u32(bytes, row + 68);
            const auto bpm = u32(bytes, row + 56);
            const auto duration = u16(bytes, row + 84);
            const auto title_offset = u16(bytes, row + 94 + 17 * 2);
            const auto file_name_offset = u16(bytes, row + 94 + 19 * 2);
            const auto path_offset = u16(bytes, row + 94 + 20 * 2);
            if (track_id == 0 || ids.contains(track_id)) return false;
            ids.emplace(track_id, true);
            const auto row_limit = page + database.page_size - row;
            std::string title, file_name, file_path;
            if (!decode_sql_string(bytes, row, row_limit, title_offset, title)
                || !decode_sql_string(bytes, row, row_limit, file_name_offset, file_name)
                || !decode_sql_string(bytes, row, row_limit, path_offset, file_path)) return false;
            if (title.empty()) title = file_name;
            if (file_path.empty()) return true;
            if (tracks.size() >= kMaxTracks) return false;
            std::string artist;
            if (const auto found = artists.find(artist_id); found != artists.end()) artist = found->second;
            tracks.push_back({.id = track_id, .title = std::move(title), .artist = std::move(artist),
                .file_path = std::move(file_path), .tempo_hundredths = bpm, .duration_seconds = duration});
            return true;
        });
}

bool safe_database_path(const fs::path& root, fs::path& database_path) {
    std::error_code error;
    const auto canonical_root = fs::canonical(root, error);
    if (error) return false;
    auto current = canonical_root;
    for (const char* component : {"PIONEER", "rekordbox", "export.pdb"}) {
        current /= component;
        const auto status = fs::symlink_status(current, error);
        if (error || fs::is_symlink(status)) return false;
        if (component == std::string_view{"export.pdb"}) {
            if (!fs::is_regular_file(status)) return false;
        } else if (!fs::is_directory(status)) {
            return false;
        }
    }
    database_path = std::move(current);
    return true;
}

} // namespace

RekordboxPdbResult read_rekordbox_pdb(const fs::path& usb_root) {
    if (usb_root.empty()) return {.error = RekordboxPdbError::NotFound};
    fs::path pdb_path;
    if (!safe_database_path(usb_root, pdb_path)) return {.error = RekordboxPdbError::NotFound};
    std::error_code error;
    const auto file_size = fs::file_size(pdb_path, error);
    if (error) return {.error = RekordboxPdbError::Io};
    if (file_size > kMaxDatabaseBytes) return {.error = RekordboxPdbError::ResourceLimit};
    try {
        std::vector<uint8_t> bytes(static_cast<std::size_t>(file_size));
        std::ifstream input(pdb_path, std::ios::binary);
        if (!input || !input.read(reinterpret_cast<char*>(bytes.data()),
                                  static_cast<std::streamsize>(bytes.size()))) {
            return {.error = RekordboxPdbError::Io};
        }
        Database database{};
        if (!read_database_header(bytes, database)) return {.error = RekordboxPdbError::Malformed};
        std::unordered_map<uint32_t, std::string> artists;
        if (!read_artist_rows(bytes, database, artists)) return {.error = RekordboxPdbError::Malformed};
        std::vector<RekordboxPdbTrack> tracks;
        if (!read_track_rows(bytes, database, artists, tracks)) {
            return {.error = tracks.size() >= kMaxTracks
                ? RekordboxPdbError::ResourceLimit : RekordboxPdbError::Malformed};
        }
        if (tracks.empty()) return {.error = RekordboxPdbError::Malformed};
        std::sort(tracks.begin(), tracks.end(), [](const auto& left, const auto& right) {
            if (left.title != right.title) return left.title < right.title;
            if (left.artist != right.artist) return left.artist < right.artist;
            return left.id < right.id;
        });
        return {.error = RekordboxPdbError::None, .tracks = std::move(tracks)};
    } catch (const std::bad_alloc&) {
        return {.error = RekordboxPdbError::ResourceLimit};
    }
}

} // namespace musicrat::backends::media
