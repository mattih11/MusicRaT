#pragma once

#include <musicrat/backends/media/decoder.hpp>
#include <musicrat/protocol/audio_block.hpp>

#include <corerat/platform/file.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <fcntl.h>
#include <unistd.h>

namespace musicrat::backends::media {

struct FfmpegAudioMetadata {
    uint32_t sample_rate_hz{0};
    uint16_t channel_count{0};
    uint64_t frame_count{0};
};

template<AVCodecID CodecId, MediaCodec MediaCodecId>
class FfmpegAudioReader {
public:
    using AudioBlock = CommRaT::Messages::AudioBlock;
    using Sample = AudioBlock::Sample;

    static constexpr MediaCodec codec = MediaCodecId;

    FfmpegAudioReader() = default;

    ~FfmpegAudioReader() {
        close();
    }

    FfmpegAudioReader(const FfmpegAudioReader&) = delete;
    FfmpegAudioReader& operator=(const FfmpegAudioReader&) = delete;

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

        auto* io_buffer = static_cast<unsigned char*>(av_malloc(io_buffer_size));
        if (io_buffer == nullptr) {
            last_error_ = DecodeError::ResourceExhausted;
            close();
            return false;
        }
        io_context_ = avio_alloc_context(
            io_buffer,
            io_buffer_size,
            0,
            this,
            &FfmpegAudioReader::read_callback,
            nullptr,
            &FfmpegAudioReader::seek_callback);
        if (io_context_ == nullptr) {
            av_free(io_buffer);
            last_error_ = DecodeError::ResourceExhausted;
            close();
            return false;
        }

        format_context_ = avformat_alloc_context();
        if (format_context_ == nullptr) {
            last_error_ = DecodeError::ResourceExhausted;
            close();
            return false;
        }
        format_context_->pb = io_context_;
        format_context_->flags |= AVFMT_FLAG_CUSTOM_IO;
        if (avformat_open_input(
                &format_context_, nullptr, nullptr, nullptr) < 0
            || avformat_find_stream_info(format_context_, nullptr) < 0) {
            last_error_ = DecodeError::UnsupportedFormat;
            close();
            return false;
        }

        const int stream_index = av_find_best_stream(
            format_context_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (stream_index < 0) {
            last_error_ = DecodeError::UnsupportedFormat;
            close();
            return false;
        }
        stream_index_ = stream_index;
        const AVCodecParameters* codec_parameters = stream().codecpar;
        if (codec_parameters->codec_id != CodecId) {
            last_error_ = DecodeError::UnsupportedFormat;
            close();
            return false;
        }

        const AVCodec* codec_impl = avcodec_find_decoder(codec_parameters->codec_id);
        if (codec_impl == nullptr) {
            last_error_ = DecodeError::UnsupportedFormat;
            close();
            return false;
        }
        codec_context_ = avcodec_alloc_context3(codec_impl);
        if (codec_context_ == nullptr) {
            last_error_ = DecodeError::ResourceExhausted;
            close();
            return false;
        }
        if (avcodec_parameters_to_context(codec_context_, codec_parameters) < 0
            || avcodec_open2(codec_context_, codec_impl, nullptr) < 0) {
            last_error_ = DecodeError::DecoderFailure;
            close();
            return false;
        }

        if (!initialize_metadata() || !initialize_resampler()) {
            close();
            return false;
        }
        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (packet_ == nullptr || frame_ == nullptr) {
            last_error_ = DecodeError::ResourceExhausted;
            close();
            return false;
        }

        last_error_ = DecodeError::None;
        return true;
    }

    void close() noexcept {
        swr_free(&resampler_);
        av_frame_free(&frame_);
        av_packet_free(&packet_);
        avcodec_free_context(&codec_context_);
        if (format_context_ != nullptr) {
            format_context_->pb = nullptr;
            avformat_close_input(&format_context_);
        }
        if (io_context_ != nullptr) {
            av_freep(&io_context_->buffer);
            avio_context_free(&io_context_);
        }
        file_.close();
        metadata_ = {};
        file_size_ = 0;
        stream_index_ = -1;
        current_frame_ = 0;
        sequence_number_ = 0;
        pending_frame_count_ = 0;
        pending_frame_offset_ = 0;
        seek_target_frame_ = 0;
        pending_discontinuity_ = false;
        seeking_ = false;
        draining_ = false;
        decoder_eof_ = false;
    }

    [[nodiscard]] bool is_open() const noexcept {
        return format_context_ != nullptr
            && codec_context_ != nullptr
            && resampler_ != nullptr
            && file_.is_open();
    }

    [[nodiscard]] const FfmpegAudioMetadata& metadata() const noexcept {
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
            || frame > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            last_error_ = DecodeError::InvalidArgument;
            return false;
        }
        const int64_t stream_timestamp = av_rescale_q(
            static_cast<int64_t>(frame),
            AVRational{1, static_cast<int>(metadata_.sample_rate_hz)},
            stream().time_base) + stream_start_timestamp();
        if (av_seek_frame(
                format_context_, stream_index_, stream_timestamp,
                AVSEEK_FLAG_BACKWARD) < 0) {
            last_error_ = DecodeError::SeekFailed;
            return false;
        }

        avcodec_flush_buffers(codec_context_);
        swr_close(resampler_);
        if (swr_init(resampler_) < 0) {
            last_error_ = DecodeError::DecoderFailure;
            return false;
        }
        pending_frame_count_ = 0;
        pending_frame_offset_ = 0;
        seek_target_frame_ = frame;
        current_frame_ = frame;
        pending_discontinuity_ = true;
        seeking_ = frame < metadata_.frame_count;
        draining_ = false;
        decoder_eof_ = frame == metadata_.frame_count;
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
        if (decoder_eof_ && pending_frame_offset_ == pending_frame_count_) {
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

        while (block.frame_count < requested_frames) {
            if (pending_frame_offset_ == pending_frame_count_) {
                pending_frame_count_ = 0;
                pending_frame_offset_ = 0;
                const auto result = decode_next_frame();
                if (result == DecodeResult::Error) {
                    clear_block(block);
                    block.flags = CommRaT::Messages::AUDIO_BLOCK_INVALID;
                    return DecodeResult::Error;
                }
                if (result == DecodeResult::EndOfStream) {
                    break;
                }
            }

            const uint32_t available = pending_frame_count_ - pending_frame_offset_;
            const uint32_t copied = std::min(
                available, requested_frames - block.frame_count);
            for (uint32_t frame_index = 0; frame_index < copied; ++frame_index) {
                const uint32_t source_frame = pending_frame_offset_ + frame_index;
                for (uint16_t channel = 0;
                     channel < metadata_.channel_count;
                     ++channel) {
                    block.channels[channel].push_back(from_pcm16(
                        interleaved_samples_[
                            source_frame * metadata_.channel_count + channel]));
                }
            }
            pending_frame_offset_ += copied;
            block.frame_count += copied;
            current_frame_ += copied;
        }

        if (block.frame_count == 0) {
            clear_block(block);
            block.flags = CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
            return DecodeResult::EndOfStream;
        }
        if ((decoder_eof_ && pending_frame_offset_ == pending_frame_count_)
            || current_frame_ >= metadata_.frame_count) {
            block.flags |= CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
        }
        last_error_ = DecodeError::None;
        return DecodeResult::Data;
    }

private:
    static constexpr int io_buffer_size = 32 * 1024;

    [[nodiscard]] AVStream& stream() noexcept {
        return *format_context_->streams[stream_index_];
    }

    [[nodiscard]] const AVStream& stream() const noexcept {
        return *format_context_->streams[stream_index_];
    }

    [[nodiscard]] int64_t stream_start_timestamp() const noexcept {
        return stream().start_time == AV_NOPTS_VALUE ? 0 : stream().start_time;
    }

    [[nodiscard]] bool initialize_metadata() noexcept {
        const int sample_rate_hz = codec_context_->sample_rate;
        const int channel_count = codec_context_->ch_layout.nb_channels;
        int64_t duration = stream().duration;
        AVRational duration_base = stream().time_base;
        if (duration <= 0 && format_context_->duration > 0) {
            duration = format_context_->duration;
            duration_base = AV_TIME_BASE_Q;
        }
        if (sample_rate_hz <= 0
            || channel_count <= 0
            || channel_count > static_cast<int>(AudioBlock::MAX_CHANNELS)
            || duration <= 0) {
            last_error_ = DecodeError::InvalidMetadata;
            return false;
        }
        const int64_t frame_count = av_rescale_q(
            duration,
            duration_base,
            AVRational{1, sample_rate_hz});
        if (frame_count <= 0) {
            last_error_ = DecodeError::InvalidMetadata;
            return false;
        }
        metadata_ = {
            .sample_rate_hz = static_cast<uint32_t>(sample_rate_hz),
            .channel_count = static_cast<uint16_t>(channel_count),
            .frame_count = static_cast<uint64_t>(frame_count),
        };
        return true;
    }

    [[nodiscard]] bool initialize_resampler() noexcept {
        if (swr_alloc_set_opts2(
                &resampler_,
                &codec_context_->ch_layout,
                AV_SAMPLE_FMT_S16,
                codec_context_->sample_rate,
                &codec_context_->ch_layout,
                codec_context_->sample_fmt,
                codec_context_->sample_rate,
                0,
                nullptr) < 0
            || resampler_ == nullptr
            || swr_init(resampler_) < 0) {
            last_error_ = DecodeError::DecoderFailure;
            return false;
        }
        return true;
    }

    [[nodiscard]] DecodeResult decode_next_frame() noexcept {
        while (true) {
            const int receive_result = avcodec_receive_frame(codec_context_, frame_);
            if (receive_result == 0) {
                if (!convert_frame()) {
                    return DecodeResult::Error;
                }
                if (pending_frame_offset_ == pending_frame_count_) {
                    continue;
                }
                return DecodeResult::Data;
            }
            if (receive_result == AVERROR_EOF) {
                decoder_eof_ = true;
                return DecodeResult::EndOfStream;
            }
            if (receive_result != AVERROR(EAGAIN)) {
                last_error_ = DecodeError::CorruptData;
                return DecodeResult::Error;
            }

            if (draining_) {
                last_error_ = DecodeError::CorruptData;
                return DecodeResult::Error;
            }

            int demux_result = 0;
            while (true) {
                demux_result = av_read_frame(format_context_, packet_);
                if (demux_result < 0 || packet_->stream_index == stream_index_) {
                    break;
                }
                av_packet_unref(packet_);
            }

            int send_result = 0;
            if (demux_result < 0) {
                draining_ = true;
                send_result = avcodec_send_packet(codec_context_, nullptr);
            } else {
                send_result = avcodec_send_packet(codec_context_, packet_);
                av_packet_unref(packet_);
            }
            if (send_result < 0 && send_result != AVERROR_EOF) {
                last_error_ = DecodeError::CorruptData;
                return DecodeResult::Error;
            }
        }
    }

    [[nodiscard]] bool convert_frame() noexcept {
        if (frame_->nb_samples <= 0
            || frame_->nb_samples > static_cast<int>(AudioBlock::MAX_FRAMES)
            || frame_->ch_layout.nb_channels != metadata_.channel_count
            || frame_->sample_rate != static_cast<int>(metadata_.sample_rate_hz)) {
            last_error_ = DecodeError::InvalidMetadata;
            return false;
        }
        unsigned char* output[] = {
            reinterpret_cast<unsigned char*>(interleaved_samples_.data())};
        const unsigned char** input = const_cast<const unsigned char**>(
            frame_->extended_data);
        const int converted = swr_convert(
            resampler_,
            output,
            static_cast<int>(AudioBlock::MAX_FRAMES),
            input,
            frame_->nb_samples);
        if (converted <= 0
            || converted > static_cast<int>(AudioBlock::MAX_FRAMES)) {
            last_error_ = DecodeError::DecoderFailure;
            return false;
        }

        pending_frame_count_ = static_cast<uint32_t>(converted);
        pending_frame_offset_ = 0;
        if (!seeking_) {
            return true;
        }

        const int64_t timestamp = frame_->best_effort_timestamp;
        if (timestamp == AV_NOPTS_VALUE) {
            seeking_ = false;
            return true;
        }
        const int64_t relative_timestamp = timestamp - stream_start_timestamp();
        const int64_t decoded_start = av_rescale_q(
            relative_timestamp,
            stream().time_base,
            AVRational{1, static_cast<int>(metadata_.sample_rate_hz)});
        if (decoded_start < 0) {
            pending_frame_count_ = 0;
            pending_frame_offset_ = 0;
            return true;
        }
        const uint64_t frame_start = static_cast<uint64_t>(decoded_start);
        const uint64_t frame_end = frame_start + pending_frame_count_;
        if (frame_end <= seek_target_frame_) {
            pending_frame_offset_ = pending_frame_count_;
            return true;
        }
        if (frame_start < seek_target_frame_) {
            pending_frame_offset_ = static_cast<uint32_t>(
                seek_target_frame_ - frame_start);
        } else if (frame_start > seek_target_frame_) {
            current_frame_ = frame_start;
        }
        seeking_ = false;
        return true;
    }

    static int read_callback(
        void* opaque, unsigned char* buffer, int buffer_size) noexcept {
        auto& self = *static_cast<FfmpegAudioReader*>(opaque);
        const auto result = self.file_.read(
            buffer, static_cast<std::size_t>(buffer_size));
        if (result < 0) {
            self.last_error_ = DecodeError::FileIo;
            return AVERROR(EIO);
        }
        return result == 0 ? AVERROR_EOF : static_cast<int>(result);
    }

    static int64_t seek_callback(
        void* opaque, int64_t offset, int whence) noexcept {
        auto& self = *static_cast<FfmpegAudioReader*>(opaque);
        if (whence == AVSEEK_SIZE) {
            return self.file_size_;
        }
        const int origin = whence & ~AVSEEK_FORCE;
        if ((origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END)
            || offset < std::numeric_limits<off_t>::min()
            || offset > std::numeric_limits<off_t>::max()) {
            return AVERROR(EINVAL);
        }
        const off_t result = self.file_.seek(static_cast<off_t>(offset), origin);
        if (result < 0) {
            self.last_error_ = DecodeError::FileIo;
            return AVERROR(EIO);
        }
        return static_cast<int64_t>(result);
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
    AVIOContext* io_context_{nullptr};
    AVFormatContext* format_context_{nullptr};
    AVCodecContext* codec_context_{nullptr};
    SwrContext* resampler_{nullptr};
    AVPacket* packet_{nullptr};
    AVFrame* frame_{nullptr};
    std::array<int16_t,
        musicrat::config::max_audio_channels
            * musicrat::config::max_audio_frames> interleaved_samples_{};
    FfmpegAudioMetadata metadata_{};
    off_t file_size_{0};
    int stream_index_{-1};
    uint64_t current_frame_{0};
    uint64_t sequence_number_{0};
    uint64_t seek_target_frame_{0};
    uint32_t pending_frame_count_{0};
    uint32_t pending_frame_offset_{0};
    bool pending_discontinuity_{false};
    bool seeking_{false};
    bool draining_{false};
    bool decoder_eof_{false};
    DecodeError last_error_{DecodeError::None};
};

using AacReader = FfmpegAudioReader<AV_CODEC_ID_AAC, MediaCodec::Aac>;

static_assert(DecoderBackend<AacReader>);

} // namespace musicrat::backends::media