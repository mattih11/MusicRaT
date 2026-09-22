#pragma once

#include <musicrat/backends/media/decoder.hpp>
#include <musicrat/protocol/audio_block.hpp>

#include <corerat/platform/file.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include <fcntl.h>
#include <unistd.h>

namespace musicrat::backends::media {

using WavReadResult = DecodeResult;

struct WavMetadata {
    uint32_t sample_rate_hz{0};
    uint16_t channel_count{0};
    uint16_t bits_per_sample{0};
    uint64_t frame_count{0};
};

class WavReader {
public:
    static constexpr MediaCodec codec = MediaCodec::PcmWav;

    WavReader() = default;

    ~WavReader() {
        close();
    }

    WavReader(const WavReader&) = delete;
    WavReader& operator=(const WavReader&) = delete;

    [[nodiscard]] bool open(
        const char* path,
        const corerat::FileConfig& config = {}) noexcept {
        close();
        last_error_ = DecodeError::None;
        if (path == nullptr || path[0] == '\0') {
            last_error_ = DecodeError::InvalidArgument;
            return false;
        }
        if (!file_.open(path, O_RDONLY, 0, config)) {
            last_error_ = DecodeError::FileOpen;
            return false;
        }
        if (!parse_header()) {
            if (last_error_ == DecodeError::None) {
                last_error_ = DecodeError::UnsupportedFormat;
            }
            close();
            return false;
        }
        last_error_ = DecodeError::None;
        return true;
    }

    void close() noexcept {
        file_.close();
        metadata_ = {};
        data_offset_ = 0;
        data_bytes_ = 0;
        current_frame_ = 0;
        sequence_number_ = 0;
        pending_discontinuity_ = false;
    }

    [[nodiscard]] bool is_open() const noexcept {
        return file_.is_open();
    }

    [[nodiscard]] const WavMetadata& metadata() const noexcept {
        return metadata_;
    }

    [[nodiscard]] uint64_t current_frame() const noexcept {
        return current_frame_;
    }

    [[nodiscard]] DecodeError last_error() const noexcept {
        return last_error_;
    }

    [[nodiscard]] bool seek_frame(uint64_t frame) noexcept {
        if (!file_.is_open() || frame > metadata_.frame_count) {
            last_error_ = DecodeError::InvalidArgument;
            return false;
        }
        const uint64_t byte_offset = frame * bytes_per_frame();
        if (byte_offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())
            || file_.seek(
                data_offset_ + static_cast<off_t>(byte_offset), SEEK_SET) < 0) {
            last_error_ = DecodeError::SeekFailed;
            return false;
        }
        current_frame_ = frame;
        pending_discontinuity_ = true;
        last_error_ = DecodeError::None;
        return true;
    }

    [[nodiscard]] DecodeResult read(
        CommRaT::Messages::AudioBlock& block,
        uint32_t requested_frames = CommRaT::Messages::AudioBlock::MAX_FRAMES) noexcept {
        clear_block(block);
        if (!file_.is_open() || requested_frames == 0
            || requested_frames > CommRaT::Messages::AudioBlock::MAX_FRAMES) {
            block.flags = CommRaT::Messages::AUDIO_BLOCK_INVALID;
            last_error_ = DecodeError::InvalidArgument;
            return DecodeResult::Error;
        }
        if (current_frame_ == metadata_.frame_count) {
            block.flags = CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
            return DecodeResult::EndOfStream;
        }

        const uint64_t remaining_frames = metadata_.frame_count - current_frame_;
        const auto frame_count = static_cast<uint32_t>(
            std::min<uint64_t>(requested_frames, remaining_frames));
        const std::size_t byte_count = static_cast<std::size_t>(frame_count)
            * bytes_per_frame();
        if (!read_all(encoded_block_.data(), byte_count)) {
            block.flags = CommRaT::Messages::AUDIO_BLOCK_INVALID;
            last_error_ = DecodeError::FileIo;
            return DecodeResult::Error;
        }

        block.sample_rate_hz = static_cast<double>(metadata_.sample_rate_hz);
        block.timestamp_ns = frame_timestamp_ns(current_frame_);
        block.sequence_number = sequence_number_++;
        block.frame_count = frame_count;
        block.channel_count = metadata_.channel_count;
        if (pending_discontinuity_) {
            block.flags |= CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
            pending_discontinuity_ = false;
        }

        std::size_t offset = 0;
        for (uint32_t frame = 0; frame < frame_count; ++frame) {
            for (uint16_t channel = 0; channel < metadata_.channel_count; ++channel) {
                const uint16_t bits = static_cast<uint16_t>(
                    std::to_integer<uint8_t>(encoded_block_[offset]))
                    | static_cast<uint16_t>(
                        std::to_integer<uint8_t>(encoded_block_[offset + 1]) << 8U);
                block.channels[channel].push_back(from_pcm16(
                    static_cast<int16_t>(bits)));
                offset += sizeof(int16_t);
            }
        }

        current_frame_ += frame_count;
        if (current_frame_ == metadata_.frame_count) {
            block.flags |= CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
        }
        last_error_ = DecodeError::None;
        return DecodeResult::Data;
    }

private:
    static constexpr uint16_t pcm_format = 1;

    static uint16_t read_u16(const std::byte* bytes) noexcept {
        return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[0]))
            | static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[1]) << 8U);
    }

    static uint32_t read_u32(const std::byte* bytes) noexcept {
        return static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[0]))
            | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[1])) << 8U)
            | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[2])) << 16U)
            | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[3])) << 24U);
    }

    static CommRaT::Messages::AudioBlock::Sample from_pcm16(int16_t sample) noexcept {
        const double value = sample < 0
            ? static_cast<double>(sample) / 32768.0
            : static_cast<double>(sample) / 32767.0;
        return static_cast<CommRaT::Messages::AudioBlock::Sample>(value);
    }

    static void clear_block(CommRaT::Messages::AudioBlock& block) noexcept {
        for (auto& channel : block.channels) {
            channel.clear();
        }
        block = {};
    }

    [[nodiscard]] uint64_t bytes_per_frame() const noexcept {
        return static_cast<uint64_t>(metadata_.channel_count) * sizeof(int16_t);
    }

    [[nodiscard]] uint64_t frame_timestamp_ns(uint64_t frame) const noexcept {
        constexpr uint64_t nanoseconds_per_second = 1'000'000'000ULL;
        const uint64_t seconds = frame / metadata_.sample_rate_hz;
        const uint64_t remainder = frame % metadata_.sample_rate_hz;
        return seconds * nanoseconds_per_second
            + remainder * nanoseconds_per_second / metadata_.sample_rate_hz;
    }

    bool parse_header() noexcept {
        const off_t file_size = file_.seek(0, SEEK_END);
        if (file_size < 12 || file_.seek(0, SEEK_SET) < 0) {
            last_error_ = DecodeError::FileIo;
            return false;
        }

        std::array<std::byte, 12> riff{};
        if (!pread_all(riff.data(), riff.size(), 0)
            || std::memcmp(riff.data(), "RIFF", 4) != 0
            || std::memcmp(riff.data() + 8, "WAVE", 4) != 0) {
            if (last_error_ == DecodeError::None) {
                last_error_ = DecodeError::UnsupportedFormat;
            }
            return false;
        }

        const uint64_t riff_end = static_cast<uint64_t>(read_u32(riff.data() + 4)) + 8U;
        if (riff_end > static_cast<uint64_t>(file_size) || riff_end < riff.size()) {
            last_error_ = DecodeError::CorruptData;
            return false;
        }

        bool has_format = false;
        bool has_data = false;
        uint64_t offset = riff.size();
        while (offset + 8U <= riff_end) {
            std::array<std::byte, 8> chunk{};
            if (!pread_all(chunk.data(), chunk.size(), static_cast<off_t>(offset))) {
                return false;
            }
            const uint32_t chunk_size = read_u32(chunk.data() + 4);
            const uint64_t payload_offset = offset + chunk.size();
            const uint64_t padded_size = static_cast<uint64_t>(chunk_size)
                + (chunk_size & 1U);
            if (payload_offset > riff_end || padded_size > riff_end - payload_offset) {
                last_error_ = DecodeError::CorruptData;
                return false;
            }

            if (std::memcmp(chunk.data(), "fmt ", 4) == 0) {
                if (chunk_size < 16 || !parse_format(static_cast<off_t>(payload_offset))) {
                    return false;
                }
                has_format = true;
            } else if (std::memcmp(chunk.data(), "data", 4) == 0 && !has_data) {
                data_offset_ = static_cast<off_t>(payload_offset);
                data_bytes_ = chunk_size;
                has_data = true;
            }
            offset = payload_offset + padded_size;
        }

        if (!has_format || !has_data || data_bytes_ % bytes_per_frame() != 0) {
            last_error_ = DecodeError::InvalidMetadata;
            return false;
        }
        metadata_.frame_count = data_bytes_ / bytes_per_frame();
        if (file_.seek(data_offset_, SEEK_SET) < 0) {
            last_error_ = DecodeError::SeekFailed;
            return false;
        }
        return true;
    }

    bool parse_format(off_t offset) noexcept {
        std::array<std::byte, 16> format{};
        if (!pread_all(format.data(), format.size(), offset)) {
            return false;
        }

        const uint16_t encoding = read_u16(format.data());
        const uint16_t channel_count = read_u16(format.data() + 2);
        const uint32_t sample_rate_hz = read_u32(format.data() + 4);
        const uint32_t byte_rate = read_u32(format.data() + 8);
        const uint16_t block_align = read_u16(format.data() + 12);
        const uint16_t bits_per_sample = read_u16(format.data() + 14);
        const uint32_t expected_block_align = channel_count * sizeof(int16_t);
        const uint64_t expected_byte_rate = static_cast<uint64_t>(sample_rate_hz)
            * expected_block_align;
        if (encoding != pcm_format || channel_count == 0
            || channel_count > CommRaT::Messages::AudioBlock::MAX_CHANNELS
            || sample_rate_hz == 0 || bits_per_sample != 16
            || block_align != expected_block_align
            || expected_byte_rate > std::numeric_limits<uint32_t>::max()
            || byte_rate != expected_byte_rate) {
            last_error_ = encoding != pcm_format || bits_per_sample != 16
                ? DecodeError::UnsupportedFormat
                : DecodeError::InvalidMetadata;
            return false;
        }

        metadata_.sample_rate_hz = sample_rate_hz;
        metadata_.channel_count = channel_count;
        metadata_.bits_per_sample = bits_per_sample;
        return true;
    }

    bool pread_all(void* data, std::size_t size, off_t offset) noexcept {
        auto* bytes = static_cast<std::byte*>(data);
        std::size_t total = 0;
        while (total < size) {
            const auto result = file_.pread(
                bytes + total,
                size - total,
                offset + static_cast<off_t>(total));
            if (result <= 0) {
                last_error_ = DecodeError::FileIo;
                return false;
            }
            total += static_cast<std::size_t>(result);
        }
        return true;
    }

    bool read_all(void* data, std::size_t size) noexcept {
        auto* bytes = static_cast<std::byte*>(data);
        std::size_t total = 0;
        while (total < size) {
            const auto result = file_.read(bytes + total, size - total);
            if (result <= 0) {
                return false;
            }
            total += static_cast<std::size_t>(result);
        }
        return true;
    }

    corerat::File file_{};
    std::array<std::byte,
        musicrat::config::max_audio_channels
            * musicrat::config::max_audio_frames
            * sizeof(int16_t)> encoded_block_{};
    WavMetadata metadata_{};
    off_t data_offset_{0};
    uint64_t data_bytes_{0};
    uint64_t current_frame_{0};
    uint64_t sequence_number_{0};
    bool pending_discontinuity_{false};
    DecodeError last_error_{DecodeError::None};
};

static_assert(DecoderBackend<WavReader>);

} // namespace musicrat::backends::media