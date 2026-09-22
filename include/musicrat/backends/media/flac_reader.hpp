#pragma once

#include <musicrat/backends/media/decoder.hpp>
#include <musicrat/protocol/audio_block.hpp>

#include <corerat/platform/file.hpp>

#include <FLAC/stream_decoder.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <fcntl.h>
#include <unistd.h>

namespace musicrat::backends::media {

struct FlacMetadata {
    uint32_t sample_rate_hz{0};
    uint16_t channel_count{0};
    uint16_t bits_per_sample{0};
    uint32_t maximum_block_frames{0};
    uint64_t frame_count{0};
};

class FlacReader {
public:
    using AudioBlock = CommRaT::Messages::AudioBlock;
    using Sample = AudioBlock::Sample;

    static constexpr MediaCodec codec = MediaCodec::Flac;

    FlacReader() = default;

    ~FlacReader() {
        close();
    }

    FlacReader(const FlacReader&) = delete;
    FlacReader& operator=(const FlacReader&) = delete;

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

        file_size_ = file_.seek(0, SEEK_END);
        if (file_size_ <= 0 || file_.seek(0, SEEK_SET) < 0) {
            last_error_ = DecodeError::FileIo;
            close();
            return false;
        }

        decoder_ = FLAC__stream_decoder_new();
        if (decoder_ == nullptr) {
            last_error_ = DecodeError::ResourceExhausted;
            close();
            return false;
        }
        const auto init_status = FLAC__stream_decoder_init_stream(
            decoder_,
            &FlacReader::read_callback,
            &FlacReader::seek_callback,
            &FlacReader::tell_callback,
            &FlacReader::length_callback,
            &FlacReader::eof_callback,
            &FlacReader::write_callback,
            &FlacReader::metadata_callback,
            &FlacReader::error_callback,
            this);
        if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK
            || !FLAC__stream_decoder_process_until_end_of_metadata(decoder_)
            || callback_failed_
            || !valid_metadata()) {
            if (last_error_ == DecodeError::None) {
                last_error_ = metadata_received_
                    ? DecodeError::InvalidMetadata
                    : DecodeError::UnsupportedFormat;
            }
            close();
            return false;
        }
        last_error_ = DecodeError::None;
        return true;
    }

    void close() noexcept {
        if (decoder_ != nullptr) {
            static_cast<void>(FLAC__stream_decoder_finish(decoder_));
            FLAC__stream_decoder_delete(decoder_);
            decoder_ = nullptr;
        }
        file_.close();
        metadata_ = {};
        file_size_ = 0;
        current_frame_ = 0;
        sequence_number_ = 0;
        pending_frame_count_ = 0;
        pending_frame_offset_ = 0;
        pending_discontinuity_ = false;
        metadata_received_ = false;
        callback_failed_ = false;
    }

    [[nodiscard]] bool is_open() const noexcept {
        return decoder_ != nullptr && file_.is_open();
    }

    [[nodiscard]] const FlacMetadata& metadata() const noexcept {
        return metadata_;
    }

    [[nodiscard]] uint64_t current_frame() const noexcept {
        return current_frame_;
    }

    [[nodiscard]] DecodeError last_error() const noexcept {
        return last_error_;
    }

    [[nodiscard]] bool seek_frame(uint64_t frame) noexcept {
        if (!is_open() || frame > metadata_.frame_count) {
            last_error_ = DecodeError::InvalidArgument;
            return false;
        }
        pending_frame_count_ = 0;
        pending_frame_offset_ = 0;
        callback_failed_ = false;
        if (frame < metadata_.frame_count
            && !FLAC__stream_decoder_seek_absolute(decoder_, frame)) {
            if (last_error_ == DecodeError::None) {
                last_error_ = DecodeError::SeekFailed;
            }
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
        if (current_frame_ == metadata_.frame_count) {
            block.flags = CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
            return DecodeResult::EndOfStream;
        }

        const uint64_t start_frame = current_frame_;
        block.sample_rate_hz = static_cast<double>(metadata_.sample_rate_hz);
        block.timestamp_ns = frame_timestamp_ns(start_frame);
        block.sequence_number = sequence_number_++;
        block.channel_count = metadata_.channel_count;
        if (pending_discontinuity_) {
            block.flags |= CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
            pending_discontinuity_ = false;
        }

        while (block.frame_count < requested_frames
            && current_frame_ < metadata_.frame_count) {
            if (pending_frame_offset_ == pending_frame_count_) {
                pending_frame_count_ = 0;
                pending_frame_offset_ = 0;
                callback_failed_ = false;
                if (!FLAC__stream_decoder_process_single(decoder_)
                    || callback_failed_) {
                    clear_block(block);
                    block.flags = CommRaT::Messages::AUDIO_BLOCK_INVALID;
                    if (last_error_ == DecodeError::None) {
                        last_error_ = DecodeError::DecoderFailure;
                    }
                    return DecodeResult::Error;
                }
                if (pending_frame_count_ == 0) {
                    if (FLAC__stream_decoder_get_state(decoder_)
                        == FLAC__STREAM_DECODER_END_OF_STREAM) {
                        break;
                    }
                    clear_block(block);
                    block.flags = CommRaT::Messages::AUDIO_BLOCK_INVALID;
                    last_error_ = DecodeError::CorruptData;
                    return DecodeResult::Error;
                }
            }

            const uint32_t available = pending_frame_count_ - pending_frame_offset_;
            const uint32_t wanted = requested_frames - block.frame_count;
            const uint32_t copied = std::min(available, wanted);
            for (uint32_t frame = 0; frame < copied; ++frame) {
                const uint32_t source_frame = pending_frame_offset_ + frame;
                for (uint16_t channel = 0;
                     channel < metadata_.channel_count;
                     ++channel) {
                    block.channels[channel].push_back(
                        pending_samples_[channel][source_frame]);
                }
            }
            pending_frame_offset_ += copied;
            block.frame_count += copied;
            current_frame_ += copied;
        }

        if (block.frame_count == 0) {
            block.flags = CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
            return DecodeResult::EndOfStream;
        }
        if (current_frame_ == metadata_.frame_count) {
            block.flags |= CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
        }
        last_error_ = DecodeError::None;
        return DecodeResult::Data;
    }

private:
    static FLAC__StreamDecoderReadStatus read_callback(
        const FLAC__StreamDecoder*,
        FLAC__byte buffer[],
        std::size_t* bytes,
        void* client_data) noexcept {
        auto& self = *static_cast<FlacReader*>(client_data);
        if (*bytes == 0) {
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        const auto result = self.file_.read(buffer, *bytes);
        if (result < 0) {
            self.last_error_ = DecodeError::FileIo;
            *bytes = 0;
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        *bytes = static_cast<std::size_t>(result);
        return result == 0
            ? FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM
            : FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

    static FLAC__StreamDecoderSeekStatus seek_callback(
        const FLAC__StreamDecoder*,
        FLAC__uint64 offset,
        void* client_data) noexcept {
        auto& self = *static_cast<FlacReader*>(client_data);
        if (offset > static_cast<FLAC__uint64>(
                std::numeric_limits<off_t>::max())) {
            self.last_error_ = DecodeError::SeekFailed;
            return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
        }
        if (self.file_.seek(static_cast<off_t>(offset), SEEK_SET) < 0) {
            self.last_error_ = DecodeError::SeekFailed;
            return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
        }
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
    }

    static FLAC__StreamDecoderTellStatus tell_callback(
        const FLAC__StreamDecoder*,
        FLAC__uint64* offset,
        void* client_data) noexcept {
        auto& self = *static_cast<FlacReader*>(client_data);
        const off_t position = self.file_.seek(0, SEEK_CUR);
        if (position < 0) {
            self.last_error_ = DecodeError::FileIo;
            return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
        }
        *offset = static_cast<FLAC__uint64>(position);
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }

    static FLAC__StreamDecoderLengthStatus length_callback(
        const FLAC__StreamDecoder*,
        FLAC__uint64* length,
        void* client_data) noexcept {
        const auto& self = *static_cast<const FlacReader*>(client_data);
        if (self.file_size_ < 0) {
            const_cast<FlacReader&>(self).last_error_ = DecodeError::FileIo;
            return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
        }
        *length = static_cast<FLAC__uint64>(self.file_size_);
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }

    static FLAC__bool eof_callback(
        const FLAC__StreamDecoder*,
        void* client_data) noexcept {
        auto& self = *static_cast<FlacReader*>(client_data);
        const off_t position = self.file_.seek(0, SEEK_CUR);
        return position < 0 || position >= self.file_size_;
    }

    static FLAC__StreamDecoderWriteStatus write_callback(
        const FLAC__StreamDecoder*,
        const FLAC__Frame* frame,
        const FLAC__int32* const buffers[],
        void* client_data) noexcept {
        auto& self = *static_cast<FlacReader*>(client_data);
        const uint32_t frame_count = frame->header.blocksize;
        const uint32_t channel_count = frame->header.channels;
        const uint32_t bits_per_sample = frame->header.bits_per_sample;
        if (frame_count == 0 || frame_count > AudioBlock::MAX_FRAMES
            || channel_count != self.metadata_.channel_count
            || bits_per_sample != self.metadata_.bits_per_sample
            || self.pending_frame_offset_ != self.pending_frame_count_) {
            self.callback_failed_ = true;
            self.last_error_ = frame_count > AudioBlock::MAX_FRAMES
                ? DecodeError::InvalidMetadata
                : DecodeError::CorruptData;
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }

        const double negative_scale = std::ldexp(1.0, bits_per_sample - 1);
        const double positive_scale = negative_scale - 1.0;
        for (uint16_t channel = 0; channel < channel_count; ++channel) {
            if (buffers[channel] == nullptr) {
                self.callback_failed_ = true;
                self.last_error_ = DecodeError::CorruptData;
                return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            }
            for (uint32_t sample = 0; sample < frame_count; ++sample) {
                const FLAC__int32 value = buffers[channel][sample];
                const double scale = value < 0 ? negative_scale : positive_scale;
                self.pending_samples_[channel][sample] = static_cast<Sample>(
                    static_cast<double>(value) / scale);
            }
        }
        self.pending_frame_count_ = frame_count;
        self.pending_frame_offset_ = 0;
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    static void metadata_callback(
        const FLAC__StreamDecoder*,
        const FLAC__StreamMetadata* metadata,
        void* client_data) noexcept {
        if (metadata->type != FLAC__METADATA_TYPE_STREAMINFO) {
            return;
        }
        auto& self = *static_cast<FlacReader*>(client_data);
        const auto& info = metadata->data.stream_info;
        self.metadata_ = {
            .sample_rate_hz = info.sample_rate,
            .channel_count = static_cast<uint16_t>(info.channels),
            .bits_per_sample = static_cast<uint16_t>(info.bits_per_sample),
            .maximum_block_frames = info.max_blocksize,
            .frame_count = info.total_samples,
        };
        self.metadata_received_ = true;
    }

    static void error_callback(
        const FLAC__StreamDecoder*,
        FLAC__StreamDecoderErrorStatus,
        void* client_data) noexcept {
        auto& self = *static_cast<FlacReader*>(client_data);
        self.callback_failed_ = true;
        self.last_error_ = DecodeError::CorruptData;
    }

    [[nodiscard]] bool valid_metadata() const noexcept {
        return metadata_received_
            && metadata_.sample_rate_hz > 0
            && metadata_.channel_count > 0
            && metadata_.channel_count <= AudioBlock::MAX_CHANNELS
            && metadata_.bits_per_sample > 1
            && metadata_.bits_per_sample <= 32
            && metadata_.maximum_block_frames > 0
            && metadata_.maximum_block_frames <= AudioBlock::MAX_FRAMES
            && metadata_.frame_count > 0;
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
    FLAC__StreamDecoder* decoder_{nullptr};
    std::array<std::array<Sample, AudioBlock::MAX_FRAMES>, AudioBlock::MAX_CHANNELS>
        pending_samples_{};
    FlacMetadata metadata_{};
    off_t file_size_{0};
    uint64_t current_frame_{0};
    uint64_t sequence_number_{0};
    uint32_t pending_frame_count_{0};
    uint32_t pending_frame_offset_{0};
    bool pending_discontinuity_{false};
    bool metadata_received_{false};
    bool callback_failed_{false};
    DecodeError last_error_{DecodeError::None};
};

static_assert(DecoderBackend<FlacReader>);

} // namespace musicrat::backends::media