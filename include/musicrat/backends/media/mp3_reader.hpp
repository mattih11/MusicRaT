#pragma once

#include <musicrat/backends/media/decoder.hpp>
#include <musicrat/protocol/audio_block.hpp>

#include <corerat/platform/file.hpp>

#include <mpg123.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <fcntl.h>
#include <unistd.h>

namespace musicrat::backends::media {

namespace detail {

class Mpg123Library {
public:
    Mpg123Library() noexcept : initialized_(mpg123_init() == MPG123_OK) {}

    ~Mpg123Library() {
        if (initialized_) {
            mpg123_exit();
        }
    }

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }

private:
    bool initialized_{false};
};

inline Mpg123Library mpg123_library{};

} // namespace detail

struct Mp3Metadata {
    uint32_t sample_rate_hz{0};
    uint16_t channel_count{0};
    uint64_t frame_count{0};
};

class Mp3Reader {
public:
    using AudioBlock = CommRaT::Messages::AudioBlock;
    using Sample = AudioBlock::Sample;

    static constexpr MediaCodec codec = MediaCodec::Mp3;

    Mp3Reader() = default;

    ~Mp3Reader() {
        close();
    }

    Mp3Reader(const Mp3Reader&) = delete;
    Mp3Reader& operator=(const Mp3Reader&) = delete;

    [[nodiscard]] bool open(
        const char* path,
        const corerat::FileConfig& config = {}) noexcept {
        close();
        last_error_ = DecodeError::None;
        if (path == nullptr || path[0] == '\0') {
            last_error_ = DecodeError::InvalidArgument;
            return false;
        }
        if (!detail::mpg123_library.initialized()) {
            last_error_ = DecodeError::DecoderFailure;
            return false;
        }
        if (!file_.open(path, O_RDONLY, 0, config)) {
            last_error_ = DecodeError::FileOpen;
            return false;
        }

        int error = MPG123_OK;
        decoder_ = mpg123_new(nullptr, &error);
        if (decoder_ == nullptr) {
            last_error_ = error == MPG123_OUT_OF_MEM
                ? DecodeError::ResourceExhausted
                : DecodeError::DecoderFailure;
            close();
            return false;
        }
        if (mpg123_format_none(decoder_) != MPG123_OK
            || !enable_pcm16_formats()
            || mpg123_replace_reader_handle(
                decoder_, &Mp3Reader::read_callback,
                &Mp3Reader::seek_callback, nullptr) != MPG123_OK
            || mpg123_open_handle(decoder_, this) != MPG123_OK) {
            last_error_ = DecodeError::DecoderFailure;
            close();
            return false;
        }
        if (mpg123_scan(decoder_) != MPG123_OK) {
            last_error_ = DecodeError::UnsupportedFormat;
            close();
            return false;
        }

        long sample_rate_hz = 0;
        int channel_count = 0;
        int encoding = 0;
        const off_t frame_count = mpg123_length(decoder_);
        if (mpg123_getformat(
                decoder_, &sample_rate_hz, &channel_count, &encoding) != MPG123_OK
            || sample_rate_hz <= 0
            || static_cast<unsigned long>(sample_rate_hz)
                > std::numeric_limits<uint32_t>::max()
            || channel_count <= 0
            || channel_count > static_cast<int>(AudioBlock::MAX_CHANNELS)
            || encoding != MPG123_ENC_SIGNED_16
            || frame_count <= 0) {
            last_error_ = DecodeError::InvalidMetadata;
            close();
            return false;
        }

        metadata_ = {
            .sample_rate_hz = static_cast<uint32_t>(sample_rate_hz),
            .channel_count = static_cast<uint16_t>(channel_count),
            .frame_count = static_cast<uint64_t>(frame_count),
        };
        if (mpg123_format_none(decoder_) != MPG123_OK
            || mpg123_format(
                decoder_, sample_rate_hz, channel_count,
                MPG123_ENC_SIGNED_16) != MPG123_OK
            || mpg123_seek(decoder_, 0, SEEK_SET) != 0) {
            last_error_ = DecodeError::SeekFailed;
            close();
            return false;
        }
        last_error_ = DecodeError::None;
        return true;
    }

    void close() noexcept {
        if (decoder_ != nullptr) {
            mpg123_delete(decoder_);
            decoder_ = nullptr;
        }
        file_.close();
        metadata_ = {};
        current_frame_ = 0;
        sequence_number_ = 0;
        pending_discontinuity_ = false;
    }

    [[nodiscard]] bool is_open() const noexcept {
        return decoder_ != nullptr && file_.is_open();
    }

    [[nodiscard]] const Mp3Metadata& metadata() const noexcept {
        return metadata_;
    }

    [[nodiscard]] uint64_t current_frame() const noexcept {
        return current_frame_;
    }

    [[nodiscard]] DecodeError last_error() const noexcept {
        return last_error_;
    }

    [[nodiscard]] bool seek_frame(uint64_t frame) noexcept {
        if (!is_open() || frame > metadata_.frame_count
            || frame > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
            last_error_ = DecodeError::InvalidArgument;
            return false;
        }
        const off_t position = mpg123_seek(
            decoder_, static_cast<off_t>(frame), SEEK_SET);
        if (position < 0 || static_cast<uint64_t>(position) != frame) {
            last_error_ = DecodeError::SeekFailed;
            return false;
        }
        current_frame_ = frame;
        pending_discontinuity_ = true;
        last_error_ = DecodeError::None;
        return true;
    }

    [[nodiscard]] DecodeResult read(
        AudioBlock& block,
        uint32_t requested_frames = AudioBlock::MAX_FRAMES) noexcept {
        clear_block(block);
        if (!is_open() || requested_frames == 0
            || requested_frames > AudioBlock::MAX_FRAMES) {
            block.flags = CommRaT::Messages::AUDIO_BLOCK_INVALID;
            last_error_ = DecodeError::InvalidArgument;
            return DecodeResult::Error;
        }
        if (current_frame_ >= metadata_.frame_count) {
            block.flags = CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
            return DecodeResult::EndOfStream;
        }

        const uint64_t remaining = metadata_.frame_count - current_frame_;
        const uint32_t frame_count = static_cast<uint32_t>(
            std::min<uint64_t>(requested_frames, remaining));
        const std::size_t bytes_per_frame = metadata_.channel_count
            * sizeof(int16_t);
        std::size_t decoded_bytes = 0;
        const int result = mpg123_read(
            decoder_, interleaved_samples_.data(),
            static_cast<std::size_t>(frame_count) * bytes_per_frame,
            &decoded_bytes);
        if ((result != MPG123_OK && result != MPG123_DONE)
            || decoded_bytes % bytes_per_frame != 0) {
            block.flags = CommRaT::Messages::AUDIO_BLOCK_INVALID;
            last_error_ = result == MPG123_NEW_FORMAT
                ? DecodeError::InvalidMetadata
                : DecodeError::CorruptData;
            return DecodeResult::Error;
        }

        const auto decoded_frames = static_cast<uint32_t>(
            decoded_bytes / bytes_per_frame);
        if (decoded_frames == 0) {
            block.flags = CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
            current_frame_ = metadata_.frame_count;
            last_error_ = DecodeError::None;
            return DecodeResult::EndOfStream;
        }

        block.sample_rate_hz = static_cast<double>(metadata_.sample_rate_hz);
        block.timestamp_ns = frame_timestamp_ns(current_frame_);
        block.sequence_number = sequence_number_++;
        block.frame_count = decoded_frames;
        block.channel_count = metadata_.channel_count;
        if (pending_discontinuity_) {
            block.flags |= CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
            pending_discontinuity_ = false;
        }
        for (uint32_t frame = 0; frame < decoded_frames; ++frame) {
            for (uint16_t channel = 0;
                 channel < metadata_.channel_count;
                 ++channel) {
                block.channels[channel].push_back(from_pcm16(
                    interleaved_samples_[frame * metadata_.channel_count + channel]));
            }
        }

        current_frame_ += decoded_frames;
        if (result == MPG123_DONE || current_frame_ >= metadata_.frame_count) {
            block.flags |= CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
            current_frame_ = metadata_.frame_count;
        }
        last_error_ = DecodeError::None;
        return DecodeResult::Data;
    }

private:
    [[nodiscard]] bool enable_pcm16_formats() noexcept {
        const long* rates = nullptr;
        std::size_t rate_count = 0;
        mpg123_rates(&rates, &rate_count);
        if (rates == nullptr || rate_count == 0) {
            return false;
        }
        for (std::size_t index = 0; index < rate_count; ++index) {
            if (mpg123_format(
                    decoder_, rates[index], MPG123_MONO | MPG123_STEREO,
                    MPG123_ENC_SIGNED_16) != MPG123_OK) {
                return false;
            }
        }
        return true;
    }

    static mpg123_ssize_t read_callback(
        void* handle, void* buffer, std::size_t count) noexcept {
        auto& self = *static_cast<Mp3Reader*>(handle);
        const auto result = self.file_.read(buffer, count);
        if (result < 0) {
            self.last_error_ = DecodeError::FileIo;
        }
        return static_cast<mpg123_ssize_t>(result);
    }

    static off_t seek_callback(
        void* handle, off_t offset, int whence) noexcept {
        auto& self = *static_cast<Mp3Reader*>(handle);
        const off_t result = self.file_.seek(offset, whence);
        if (result < 0) {
            self.last_error_ = DecodeError::FileIo;
        }
        return result;
    }

    static Sample from_pcm16(int16_t sample) noexcept {
        const double value = sample < 0
            ? static_cast<double>(sample) / 32768.0
            : static_cast<double>(sample) / 32767.0;
        return static_cast<Sample>(value);
    }

    static void clear_block(AudioBlock& block) noexcept {
        for (auto& channel : block.channels) {
            channel.clear();
        }
        block = {};
    }

    [[nodiscard]] uint64_t frame_timestamp_ns(uint64_t frame) const noexcept {
        constexpr uint64_t nanoseconds_per_second = 1'000'000'000ULL;
        const uint64_t seconds = frame / metadata_.sample_rate_hz;
        const uint64_t remainder = frame % metadata_.sample_rate_hz;
        return seconds * nanoseconds_per_second
            + remainder * nanoseconds_per_second / metadata_.sample_rate_hz;
    }

    corerat::File file_{};
    mpg123_handle* decoder_{nullptr};
    std::array<int16_t,
        musicrat::config::max_audio_channels
            * musicrat::config::max_audio_frames> interleaved_samples_{};
    Mp3Metadata metadata_{};
    uint64_t current_frame_{0};
    uint64_t sequence_number_{0};
    bool pending_discontinuity_{false};
    DecodeError last_error_{DecodeError::None};
};

static_assert(DecoderBackend<Mp3Reader>);

} // namespace musicrat::backends::media