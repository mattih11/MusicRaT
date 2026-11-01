#include <musicrat/backends/media/rekordbox_anlz.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace musicrat::backends::media {
namespace {

constexpr std::size_t kMaxAnlzFileBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaxAnlzTotalBytes = 256U * 1024U * 1024U;
constexpr std::size_t kMaxAnlzFiles = 4096;
constexpr std::size_t kMaxBeatRecords = 8192;
constexpr std::size_t kMaxPathBytes = 4096;

uint16_t be16(const std::vector<uint8_t>& bytes, std::size_t offset) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8U)
        | bytes[offset + 1]);
}

uint32_t be32(const std::vector<uint8_t>& bytes, std::size_t offset) {
    return (static_cast<uint32_t>(bytes[offset]) << 24U)
        | (static_cast<uint32_t>(bytes[offset + 1]) << 16U)
        | (static_cast<uint32_t>(bytes[offset + 2]) << 8U)
        | static_cast<uint32_t>(bytes[offset + 3]);
}

bool has_tag(const std::vector<uint8_t>& bytes, std::size_t offset, std::string_view tag) {
    return tag.size() == 4 && offset + tag.size() <= bytes.size()
        && std::equal(tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

struct Section {
    std::size_t offset{0};
    std::size_t header_length{0};
    std::size_t tag_length{0};
};

struct BeatRecord {
    uint32_t time_ms{0};
    uint16_t tempo_hundredths{0};
    uint16_t beat_number{0};
};

struct ParsedCue {
    uint32_t time_ms{0};
    uint32_t loop_time_ms{0};
    uint32_t hotcue_id{0};
    bool hot{false};
};

struct ParsedAnlz {
    std::string path{};
    std::array<BeatRecord, kMaxBeatRecords> beats{};
    std::size_t beat_count{0};
    std::array<ParsedCue, 8> cues{};
    std::size_t cue_count{0};
    bool has_pco2{false};
    bool malformed_data{false};
};

bool append_utf8(std::string& output, uint32_t codepoint) {
    if (codepoint <= 0x7fU) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0x10ffffU) {
        output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        return false;
    }
    return true;
}

bool decode_path(const std::vector<uint8_t>& bytes, const Section& section, std::string& path) {
    if (section.header_length < 16 || section.offset + 16 > bytes.size()) return false;
    const auto byte_count = static_cast<std::size_t>(be32(bytes, section.offset + 12));
    const auto begin = section.offset + section.header_length;
    if ((byte_count & 1U) != 0 || byte_count < 2 || byte_count > kMaxPathBytes
        || byte_count > section.tag_length - section.header_length
        || begin + byte_count > section.offset + section.tag_length) return false;

    path.clear();
    for (std::size_t i = 0; i + 1 < byte_count; i += 2) {
        uint32_t codepoint = be16(bytes, begin + i);
        if (codepoint == 0) break;
        if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
            if (i + 3 >= byte_count) return false;
            const auto low = be16(bytes, begin + i + 2);
            if (low < 0xdc00U || low > 0xdfffU) return false;
            codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) + (low - 0xdc00U);
            i += 2;
        } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
            return false;
        }
        if (!append_utf8(path, codepoint)) return false;
    }
    return !path.empty();
}

std::string normalized_path(std::string_view path) {
    std::string result;
    result.reserve(path.size());
    bool previous_slash = false;
    for (const unsigned char ch : path) {
        if (ch == '/' || ch == '\\') {
            if (!previous_slash) result.push_back('/');
            previous_slash = true;
        } else {
            result.push_back(static_cast<char>(ch < 0x80U
                ? std::tolower(ch) : ch));
            previous_slash = false;
        }
    }
    while (!result.empty() && result.front() == '/') result.erase(result.begin());
    while (!result.empty() && result.back() == '/') result.pop_back();
    return result;
}

bool sections(const std::vector<uint8_t>& bytes, std::vector<Section>& result) {
    if (bytes.size() < 28 || !has_tag(bytes, 0, "PMAI")) return false;
    const auto header_length = static_cast<std::size_t>(be32(bytes, 4));
    const auto file_length = static_cast<std::size_t>(be32(bytes, 8));
    if (header_length < 12 || header_length > file_length || file_length > bytes.size()) return false;

    std::size_t offset = header_length;
    while (offset < file_length) {
        if (file_length - offset < 12) return false;
        const auto section_header = static_cast<std::size_t>(be32(bytes, offset + 4));
        const auto tag_length = static_cast<std::size_t>(be32(bytes, offset + 8));
        if (section_header < 12 || section_header > tag_length
            || tag_length > file_length - offset || result.size() == 4096) return false;
        result.push_back({offset, section_header, tag_length});
        offset += tag_length;
    }
    return offset == file_length;
}

bool parse_beats(const std::vector<uint8_t>& bytes, const Section& section, ParsedAnlz& parsed) {
    if (section.header_length < 24) return false;
    const auto count = static_cast<std::size_t>(be32(bytes, section.offset + 20));
    if (count > kMaxBeatRecords
        || count > (section.tag_length - section.header_length) / 8) return false;
    const auto begin = section.offset + section.header_length;
    for (std::size_t i = 0; i < count; ++i) {
        const auto record = begin + i * 8;
        const auto beat_number = be16(bytes, record);
        const auto tempo = be16(bytes, record + 2);
        const auto time = be32(bytes, record + 4);
        if (tempo == 0 || beat_number == 0
            || (parsed.beat_count != 0 && time <= parsed.beats[parsed.beat_count - 1].time_ms)) {
            return false;
        }
        parsed.beats[parsed.beat_count++] = {time, tempo, beat_number};
    }
    return true;
}

void add_cue(ParsedAnlz& parsed, uint32_t time_ms, uint32_t loop_time_ms,
             uint32_t hotcue_id, bool hot) {
    if (parsed.cue_count == parsed.cues.size()) return;
    parsed.cues[parsed.cue_count++] = {time_ms, loop_time_ms, hotcue_id, hot};
}

bool parse_pco2(const std::vector<uint8_t>& bytes, const Section& section, ParsedAnlz& parsed) {
    if (section.header_length < 20) return false;
    const bool hot_cues = be32(bytes, section.offset + 12) == 1;
    const auto count = static_cast<std::size_t>(be16(bytes, section.offset + 16));
    std::size_t cursor = section.offset + section.header_length;
    const auto end = section.offset + section.tag_length;
    bool valid_entry = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (end - cursor < 12 || !has_tag(bytes, cursor, "PCP2")) {
            parsed.malformed_data = true;
            break;
        }
        const auto entry_header = static_cast<std::size_t>(be32(bytes, cursor + 4));
        const auto entry_length = static_cast<std::size_t>(be32(bytes, cursor + 8));
        if (entry_header < 12 || entry_header > entry_length || entry_length < 28
            || entry_length > end - cursor) {
            parsed.malformed_data = true;
            break;
        }
        const auto hotcue_id = be32(bytes, cursor + 12);
        const auto time_ms = be32(bytes, cursor + 20);
        const auto loop_time_ms = be32(bytes, cursor + 24);
        if (!valid_entry) parsed.cue_count = 0;
        add_cue(parsed, time_ms, loop_time_ms, hotcue_id, hot_cues);
        cursor += entry_length;
        valid_entry = true;
    }
    parsed.has_pco2 |= valid_entry;
    return true;
}

bool parse_pcob(const std::vector<uint8_t>& bytes, const Section& section, ParsedAnlz& parsed) {
    if (section.header_length < 24) return false;
    const bool hot_cues = be32(bytes, section.offset + 12) == 1;
    const auto count = static_cast<std::size_t>(be16(bytes, section.offset + 18));
    std::size_t cursor = section.offset + section.header_length;
    const auto end = section.offset + section.tag_length;
    for (std::size_t i = 0; i < count; ++i) {
        if (end - cursor < 12 || !has_tag(bytes, cursor, "PCPT")) {
            parsed.malformed_data = true;
            break;
        }
        const auto entry_header = static_cast<std::size_t>(be32(bytes, cursor + 4));
        const auto entry_length = static_cast<std::size_t>(be32(bytes, cursor + 8));
        if (entry_header < 12 || entry_header > entry_length || entry_length < 40
            || entry_length > end - cursor) {
            parsed.malformed_data = true;
            break;
        }
        const auto hotcue_id = be32(bytes, cursor + 12);
        const auto time_ms = be32(bytes, cursor + 32);
        const auto loop_time_ms = be32(bytes, cursor + 36);
        add_cue(parsed, time_ms, loop_time_ms, hotcue_id, hot_cues);
        cursor += entry_length;
    }
    return true;
}

bool parse_file(const std::vector<uint8_t>& bytes, ParsedAnlz& parsed) {
    std::vector<Section> found;
    if (!sections(bytes, found)) return false;
    bool has_path = false;
    for (const auto& section : found) {
        if (has_tag(bytes, section.offset, "PPTH")) {
            has_path = decode_path(bytes, section, parsed.path);
            if (!has_path) return false;
        } else if (has_tag(bytes, section.offset, "PQTZ")) {
            if (!parse_beats(bytes, section, parsed)) {
                parsed.beat_count = 0;
                parsed.malformed_data = true;
            }
        } else if (has_tag(bytes, section.offset, "PCO2")) {
            if (!parse_pco2(bytes, section, parsed)) parsed.malformed_data = true;
        } else if (has_tag(bytes, section.offset, "PCOB")) {
            if (!parse_pcob(bytes, section, parsed)) parsed.malformed_data = true;
        }
    }
    return has_path;
}

bool read_file(const std::filesystem::path& path, std::vector<uint8_t>& bytes,
               std::size_t& total_read, RekordboxImportError& error_result) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        error_result = RekordboxImportError::Io;
        return false;
    }
    if (size > kMaxAnlzFileBytes || total_read > kMaxAnlzTotalBytes
        || size > kMaxAnlzTotalBytes - total_read) {
        error_result = RekordboxImportError::ResourceLimit;
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error_result = RekordboxImportError::Io;
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error_result = RekordboxImportError::Io;
        return false;
    }
    total_read += bytes.size();
    return true;
}

bool inside(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto root_it = root.begin();
    auto path_it = path.begin();
    for (; root_it != root.end(); ++root_it, ++path_it) {
        if (path_it == path.end() || *root_it != *path_it) return false;
    }
    return true;
}

bool set_grid(const ParsedAnlz& parsed, uint32_t sample_rate_hz, uint64_t duration_frames,
              CommRaT::Messages::BeatGrid& grid) {
    using namespace CommRaT::Messages;
    if (parsed.beat_count == 0 || sample_rate_hz == 0 || duration_frames == 0) return false;
    const auto first_frame = static_cast<uint64_t>(std::llround(
        static_cast<double>(parsed.beats[0].time_ms) * sample_rate_hz / 1000.0));
    if (first_frame >= duration_frames) return false;
    grid.sample_rate_hz = sample_rate_hz;
    grid.confidence_per_mille = 1000;
    grid.flags = BEAT_GRID_FLAG_IMPORTED;
    uint16_t meter = 1;
    for (std::size_t i = 0; i < parsed.beat_count; ++i) {
        meter = std::max(meter, parsed.beats[i].beat_number);
    }
    grid.beats_per_bar = meter <= 12 ? meter : 4;
    grid.beat_unit = 4;
    const double first_tempo = parsed.beats[0].tempo_hundredths / 100.0;
    grid.segments.push_back({.start_frame = first_frame, .start_beat = 0.0,
                             .tempo_bpm = first_tempo});

    std::size_t segment_start_event = 0;
    for (std::size_t i = 1; i < parsed.beat_count; ++i) {
        const auto& previous = parsed.beats[i - 1];
        const auto& current = parsed.beats[i];
        if (current.tempo_hundredths == previous.tempo_hundredths) continue;
        if (grid.segments.size() == MAX_BEAT_GRID_SEGMENTS) return false;
        const auto& prior_segment = grid.segments.back();
        const double beats_since = static_cast<double>(i - segment_start_event);
        const double exact_frame = static_cast<double>(prior_segment.start_frame)
            + beats_since * 60.0 * sample_rate_hz / prior_segment.tempo_bpm;
        if (!std::isfinite(exact_frame) || exact_frame < 0
            || exact_frame >= static_cast<double>(duration_frames)) return false;
        const auto start_frame = static_cast<uint64_t>(std::llround(exact_frame));
        if (start_frame <= prior_segment.start_frame) return false;
        const double start_beat = prior_segment.start_beat
            + static_cast<double>(start_frame - prior_segment.start_frame)
                * prior_segment.tempo_bpm / (60.0 * sample_rate_hz);
        grid.segments.push_back({.start_frame = start_frame, .start_beat = start_beat,
                                 .tempo_bpm = current.tempo_hundredths / 100.0});
        segment_start_event = i;
    }
    return !grid.segments.empty();
}

} // namespace

RekordboxImportResult import_rekordbox_anlz(
    const std::filesystem::path& usb_root,
    const std::filesystem::path& media_path,
    uint32_t sample_rate_hz,
    uint64_t duration_frames) {
    namespace fs = std::filesystem;
    using namespace CommRaT::Messages;
    if (usb_root.empty() || media_path.empty() || sample_rate_hz == 0 || duration_frames == 0) {
        return {.error = RekordboxImportError::InvalidRequest};
    }

    std::error_code error;
    const auto canonical_root = fs::canonical(usb_root, error);
    if (error) return {.error = RekordboxImportError::Io};
    const auto canonical_media = fs::canonical(media_path, error);
    if (error || !inside(canonical_root, canonical_media) || canonical_media == canonical_root) {
        return {.error = RekordboxImportError::InvalidRequest};
    }
    const auto relative_media = canonical_media.lexically_relative(canonical_root).generic_string();
    const auto wanted_path = normalized_path(relative_media);
    if (wanted_path.empty()) return {.error = RekordboxImportError::InvalidRequest};
    const auto anlz_root = canonical_root / "PIONEER" / "USBANLZ";
    const auto anlz_status = fs::symlink_status(anlz_root, error);
    if (error) {
        return {.error = error == std::errc::no_such_file_or_directory
            ? RekordboxImportError::NotFound : RekordboxImportError::Io};
    }
    if (!fs::is_directory(anlz_status)) return {.error = RekordboxImportError::NotFound};
    const auto canonical_anlz = fs::canonical(anlz_root, error);
    if (error || !inside(canonical_root, canonical_anlz)) {
        return {.error = RekordboxImportError::InvalidRequest};
    }

    std::size_t file_count = 0;
    std::size_t entry_count = 0;
    std::size_t total_read = 0;
    bool malformed_seen = false;
    bool io_error_seen = false;
    std::error_code iter_error;
    fs::recursive_directory_iterator it(anlz_root, fs::directory_options::skip_permission_denied, iter_error);
    const fs::recursive_directory_iterator end;
    if (iter_error) return {.error = RekordboxImportError::Io};
    for (; it != end; it.increment(iter_error)) {
        if (iter_error) {
            io_error_seen = true;
            iter_error.clear();
            continue;
        }
        if (++entry_count > 16384) return {.error = RekordboxImportError::ResourceLimit};
        if (it.depth() > 5) {
            it.disable_recursion_pending();
            continue;
        }
        const auto status = it->symlink_status(iter_error);
        if (iter_error) {
            io_error_seen = true;
            iter_error.clear();
            continue;
        }
        if (!fs::is_regular_file(status) || it->path().extension() != ".DAT") continue;
        if (++file_count > kMaxAnlzFiles) return {.error = RekordboxImportError::ResourceLimit};

        std::vector<uint8_t> bytes;
        ParsedAnlz parsed{};
        RekordboxImportError read_error{RekordboxImportError::None};
        if (!read_file(it->path(), bytes, total_read, read_error)) {
            if (read_error == RekordboxImportError::ResourceLimit) return {.error = read_error};
            io_error_seen |= read_error == RekordboxImportError::Io;
            continue;
        }
        if (!parse_file(bytes, parsed)) {
            malformed_seen = true;
            continue;
        }
        if (normalized_path(parsed.path) != wanted_path) continue;

        const auto sibling_ext = it->path().parent_path() / "ANLZ0000.EXT";
        const auto ext_status = fs::symlink_status(sibling_ext, iter_error);
        if (!iter_error && fs::is_regular_file(ext_status)) {
            std::vector<uint8_t> ext_bytes;
            ParsedAnlz ext_parsed{};
            read_error = RekordboxImportError::None;
            if (!read_file(sibling_ext, ext_bytes, total_read, read_error)) {
                if (read_error == RekordboxImportError::ResourceLimit) return {.error = read_error};
                io_error_seen |= read_error == RekordboxImportError::Io;
            } else if (parse_file(ext_bytes, ext_parsed)
                && normalized_path(ext_parsed.path) == wanted_path) {
                if (ext_parsed.has_pco2) {
                    parsed.cues = ext_parsed.cues;
                    parsed.cue_count = ext_parsed.cue_count;
                    parsed.has_pco2 = true;
                }
                parsed.malformed_data |= ext_parsed.malformed_data;
            }
        }
        iter_error.clear();

        RekordboxTrackMetadata metadata{};
        metadata.grid_set = set_grid(parsed, sample_rate_hz, duration_frames, metadata.beat_grid);
        metadata.cue_count = 0;
        for (std::size_t i = 0; i < parsed.cue_count; ++i) {
            const auto frame = static_cast<uint64_t>(std::llround(
                static_cast<double>(parsed.cues[i].time_ms) * sample_rate_hz / 1000.0));
            if (frame >= duration_frames) continue;
            const bool loop = parsed.cues[i].loop_time_ms > parsed.cues[i].time_ms;
            metadata.cues[metadata.cue_count++] = {
                .frame = frame,
                .hot_cue = static_cast<uint8_t>(std::min(parsed.cues[i].hotcue_id, 255U)),
                .hot = parsed.cues[i].hot,
                .loop = loop,
            };
            if (loop && !metadata.markers.loop_start_set) {
                const auto loop_end = static_cast<uint64_t>(std::llround(
                    static_cast<double>(parsed.cues[i].loop_time_ms) * sample_rate_hz / 1000.0));
                if (loop_end >= duration_frames || loop_end <= frame) continue;
                metadata.markers.loop_start_frame = frame;
                metadata.markers.loop_end_frame = loop_end;
                metadata.markers.loop_start_set = true;
                metadata.markers.loop_end_set = true;
            }
            if (!metadata.markers.cue_set) {
                metadata.markers.cue_frame = frame;
                metadata.markers.cue_set = true;
            }
        }
        metadata.cues_set = metadata.cue_count != 0;
        return {.error = (metadata.grid_set || metadata.cues_set)
                ? RekordboxImportError::None
                : parsed.malformed_data ? RekordboxImportError::Malformed
                : RekordboxImportError::NotFound,
            .metadata = metadata};
    }
    if (malformed_seen) return {.error = RekordboxImportError::Malformed};
    if (io_error_seen) return {.error = RekordboxImportError::Io};
    return {.error = RekordboxImportError::NotFound};
}

} // namespace musicrat::backends::media
