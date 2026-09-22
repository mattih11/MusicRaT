#pragma once

#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <corerat/platform/file.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include <fcntl.h>

namespace musicrat::backends::audio {

class WavWriter {
public:
    WavWriter() = default;

    ~WavWriter() {
        close();
    }

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    [[nodiscard]] bool open(
        const char* path,
        uint32_t sample_rate_hz,
        uint16_t channel_count,
        const corerat::FileConfig& config = {}) noexcept {
        close();
        if (path == nullptr || path[0] == '\0' || sample_rate_hz == 0
            || channel_count == 0
            || channel_count > CommRaT::Messages::AudioBlock::MAX_CHANNELS) {
            return false;
        }

        if (!file_.open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644, config)) {
            return false;
        }
        sample_rate_hz_ = sample_rate_hz;
        channel_count_ = channel_count;
        data_bytes_ = 0;
        const auto header = make_header(0);
        if (!write_all(header.data(), header.size())) {
            close();
            return false;
        }
        return true;
    }

    [[nodiscard]] bool write(const CommRaT::Messages::AudioBlock& block) {
        if (!file_.is_open()
            || validate_audio_block(block) != AudioBlockValidationError::None
            || block.channel_count != channel_count_
            || static_cast<uint32_t>(std::llround(block.sample_rate_hz)) != sample_rate_hz_) {
            return false;
        }

        const auto block_bytes = static_cast<uint64_t>(block.frame_count)
            * block.channel_count * sizeof(int16_t);
        if (block_bytes > encoded_block_.size()
            || data_bytes_ > std::numeric_limits<uint32_t>::max() - block_bytes) {
            return false;
        }

        std::size_t offset = 0;
        for (uint32_t frame = 0; frame < block.frame_count; ++frame) {
            for (uint16_t channel = 0; channel < block.channel_count; ++channel) {
                const auto sample = static_cast<uint16_t>(
                    to_pcm16(block.channels[channel][frame]));
                encoded_block_[offset++] = static_cast<std::byte>(sample & 0xFFU);
                encoded_block_[offset++] = static_cast<std::byte>((sample >> 8U) & 0xFFU);
            }
        }
        if (!write_all(encoded_block_.data(), offset)) {
            return false;
        }
        data_bytes_ += static_cast<uint32_t>(block_bytes);
        return true;
    }

    void close() noexcept {
        if (!file_.is_open()) {
            return;
        }
        const auto riff_size = little_endian_u32(36U + data_bytes_);
        const auto data_size = little_endian_u32(data_bytes_);
        (void)file_.pwrite(riff_size.data(), riff_size.size(), 4);
        (void)file_.pwrite(data_size.data(), data_size.size(), 40);
        (void)file_.sync();
        file_.close();
    }

    [[nodiscard]] bool is_open() const noexcept {
        return file_.is_open();
    }

private:
    static int16_t to_pcm16(CommRaT::Messages::AudioBlock::Sample sample) noexcept {
        const double value = std::clamp(static_cast<double>(sample), -1.0, 1.0);
        if (value <= -1.0) {
            return std::numeric_limits<int16_t>::min();
        }
        return static_cast<int16_t>(std::lround(value * 32767.0));
    }

    static std::array<std::byte, 4> little_endian_u32(uint32_t value) noexcept {
        return {
            static_cast<std::byte>(value & 0xFFU),
            static_cast<std::byte>((value >> 8U) & 0xFFU),
            static_cast<std::byte>((value >> 16U) & 0xFFU),
            static_cast<std::byte>((value >> 24U) & 0xFFU),
        };
    }

    static void put_u16(
        std::array<std::byte, 44>& header,
        std::size_t offset,
        uint16_t value) noexcept {
        header[offset] = static_cast<std::byte>(value & 0xFFU);
        header[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    }

    static void put_u32(
        std::array<std::byte, 44>& header,
        std::size_t offset,
        uint32_t value) noexcept {
        const auto bytes = little_endian_u32(value);
        std::copy(bytes.begin(), bytes.end(), header.begin() + offset);
    }

    std::array<std::byte, 44> make_header(uint32_t data_bytes) const noexcept {
        std::array<std::byte, 44> header{};
        std::memcpy(header.data(), "RIFF", 4);
        put_u32(header, 4, 36U + data_bytes);
        std::memcpy(header.data() + 8, "WAVEfmt ", 8);
        put_u32(header, 16, 16);
        put_u16(header, 20, 1);
        put_u16(header, 22, channel_count_);
        put_u32(header, 24, sample_rate_hz_);
        put_u32(header, 28, sample_rate_hz_ * channel_count_ * sizeof(int16_t));
        put_u16(header, 32, static_cast<uint16_t>(channel_count_ * sizeof(int16_t)));
        put_u16(header, 34, 16);
        std::memcpy(header.data() + 36, "data", 4);
        put_u32(header, 40, data_bytes);
        return header;
    }

    bool write_all(const void* data, std::size_t size) noexcept {
        const auto* bytes = static_cast<const std::byte*>(data);
        std::size_t written = 0;
        while (written < size) {
            const auto result = file_.write(bytes + written, size - written);
            if (result <= 0) {
                return false;
            }
            written += static_cast<std::size_t>(result);
        }
        return true;
    }

    corerat::File file_{};
    std::array<std::byte,
        musicrat::config::max_audio_channels
            * musicrat::config::max_audio_frames
            * sizeof(int16_t)> encoded_block_{};
    uint32_t sample_rate_hz_{0};
    uint16_t channel_count_{0};
    uint32_t data_bytes_{0};
};

} // namespace musicrat::backends::audio