#pragma once

#include <musicrat/backends/media/decoded_audio_pool.hpp>
#include <musicrat/backends/media/decoder.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace musicrat::backends::media {

enum class DecodeAheadState : uint8_t {
    Closed,
    Ready,
    EndOfStream,
    Error,
};

struct DecodeAheadRefillResult {
    std::size_t chunks_published{0};
    DecodeAheadState state{DecodeAheadState::Closed};
};

template<DecoderBackend Decoder, std::size_t ChunkCount>
class DecodeAhead {
public:
    using Pool = DecodedAudioPool<ChunkCount>;

    explicit DecodeAhead(
        uint32_t chunk_frames = CommRaT::Messages::AudioBlock::MAX_FRAMES,
        std::size_t high_watermark = ChunkCount) noexcept
        : chunk_frames_(std::clamp(
            chunk_frames,
            uint32_t{1},
            static_cast<uint32_t>(CommRaT::Messages::AudioBlock::MAX_FRAMES)))
        , high_watermark_(std::clamp(
            high_watermark,
            std::size_t{1},
            ChunkCount)) {}

    explicit DecodeAhead(
        Decoder decoder,
        uint32_t chunk_frames = CommRaT::Messages::AudioBlock::MAX_FRAMES,
        std::size_t high_watermark = ChunkCount) noexcept
        requires std::movable<Decoder>
        : decoder_(std::move(decoder))
        , chunk_frames_(std::clamp(
            chunk_frames,
            uint32_t{1},
            static_cast<uint32_t>(CommRaT::Messages::AudioBlock::MAX_FRAMES)))
        , high_watermark_(std::clamp(
            high_watermark,
            std::size_t{1},
            ChunkCount)) {}

    [[nodiscard]] bool open(const char* path) noexcept {
        decoder_.close();
        advance_generation();
        pool_.discard_before_generation(generation_);
        if (!decoder_.open(path)) {
            last_error_ = decoder_.last_error();
            state_ = DecodeAheadState::Error;
            return false;
        }
        last_error_ = DecodeError::None;
        state_ = DecodeAheadState::Ready;
        return true;
    }

    void close() noexcept {
        decoder_.close();
        advance_generation();
        pool_.discard_before_generation(generation_);
        last_error_ = DecodeError::None;
        state_ = DecodeAheadState::Closed;
    }

    [[nodiscard]] bool seek(uint64_t media_frame) noexcept {
        if (!decoder_.seek_frame(media_frame)) {
            last_error_ = decoder_.last_error();
            return false;
        }
        advance_generation();
        pool_.discard_before_generation(generation_);
        last_error_ = DecodeError::None;
        state_ = media_frame == decoder_.metadata().frame_count
            ? DecodeAheadState::EndOfStream
            : DecodeAheadState::Ready;
        return true;
    }

    [[nodiscard]] DecodeAheadRefillResult refill() noexcept {
        DecodeAheadRefillResult result{.state = state_};
        if (state_ != DecodeAheadState::Ready) {
            return result;
        }

        while (pool_.ready_count() < high_watermark_) {
            auto reservation = pool_.try_begin_write();
            if (!reservation) {
                break;
            }

            const uint64_t start_frame = decoder_.current_frame();
            const auto read_result = decoder_.read(*reservation.audio, chunk_frames_);
            if (read_result == DecodeResult::Error) {
                pool_.cancel(reservation);
                last_error_ = decoder_.last_error();
                state_ = DecodeAheadState::Error;
                break;
            }
            if (read_result == DecodeResult::EndOfStream) {
                pool_.cancel(reservation);
                state_ = DecodeAheadState::EndOfStream;
                break;
            }
            const bool is_final_chunk = (reservation.audio->flags
                & CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM) != 0;
            if (!pool_.publish(reservation, start_frame, generation_)) {
                last_error_ = DecodeError::PoolFailure;
                state_ = DecodeAheadState::Error;
                break;
            }

            ++result.chunks_published;
            if (is_final_chunk) {
                state_ = DecodeAheadState::EndOfStream;
                break;
            }
        }

        result.state = state_;
        return result;
    }

    [[nodiscard]] Pool& pool() noexcept {
        return pool_;
    }

    [[nodiscard]] const Pool& pool() const noexcept {
        return pool_;
    }

    [[nodiscard]] const auto& metadata() const noexcept {
        return decoder_.metadata();
    }

    [[nodiscard]] uint64_t generation() const noexcept {
        return generation_;
    }

    [[nodiscard]] DecodeAheadState state() const noexcept {
        return state_;
    }

    [[nodiscard]] DecodeError last_error() const noexcept {
        return last_error_;
    }

private:
    void advance_generation() noexcept {
        ++generation_;
        if (generation_ == 0) {
            ++generation_;
        }
    }

    Decoder decoder_;
    Pool pool_{};
    uint32_t chunk_frames_;
    std::size_t high_watermark_;
    uint64_t generation_{0};
    DecodeAheadState state_{DecodeAheadState::Closed};
    DecodeError last_error_{DecodeError::None};
};

} // namespace musicrat::backends::media