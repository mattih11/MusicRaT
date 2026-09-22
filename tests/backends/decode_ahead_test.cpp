#include <musicrat/backends/media/decode_ahead.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.0001;
}

struct FakeMetadata {
    uint32_t sample_rate_hz{32000};
    uint16_t channel_count{2};
    uint64_t frame_count{5};
};

class FakeDecoder {
public:
    [[nodiscard]] bool open(const char* path) noexcept {
        open_ = path != nullptr && std::strcmp(path, "memory:test") == 0;
        current_frame_ = 0;
        return open_;
    }

    void close() noexcept {
        open_ = false;
        current_frame_ = 0;
    }

    [[nodiscard]] bool seek_frame(uint64_t frame) noexcept {
        if (!open_ || frame > metadata_.frame_count) {
            return false;
        }
        current_frame_ = frame;
        discontinuity_ = true;
        return true;
    }

    [[nodiscard]] uint64_t current_frame() const noexcept {
        return current_frame_;
    }

    [[nodiscard]] musicrat::backends::media::DecodeError last_error() const noexcept {
        return last_error_;
    }

    [[nodiscard]] const FakeMetadata& metadata() const noexcept {
        return metadata_;
    }

    [[nodiscard]] musicrat::backends::media::DecodeResult read(
        CommRaT::Messages::AudioBlock& block,
        uint32_t requested_frames) noexcept {
        block = {};
        if (!open_ || requested_frames == 0) {
            last_error_ = musicrat::backends::media::DecodeError::InvalidArgument;
            return musicrat::backends::media::DecodeResult::Error;
        }
        if (current_frame_ == metadata_.frame_count) {
            block.flags = CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
            return musicrat::backends::media::DecodeResult::EndOfStream;
        }

        block.sample_rate_hz = metadata_.sample_rate_hz;
        block.channel_count = metadata_.channel_count;
        block.frame_count = static_cast<uint32_t>(std::min<uint64_t>(
            requested_frames,
            metadata_.frame_count - current_frame_));
        if (discontinuity_) {
            block.flags |= CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
            discontinuity_ = false;
        }
        for (uint32_t frame = 0; frame < block.frame_count; ++frame) {
            const auto sample = static_cast<double>(current_frame_ + frame) / 10.0;
            block.channels[0].push_back(sample);
            block.channels[1].push_back(-sample);
        }
        current_frame_ += block.frame_count;
        if (current_frame_ == metadata_.frame_count) {
            block.flags |= CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
        }
        return musicrat::backends::media::DecodeResult::Data;
    }

private:
    FakeMetadata metadata_{};
    uint64_t current_frame_{0};
    bool open_{false};
    bool discontinuity_{false};
    musicrat::backends::media::DecodeError last_error_{
        musicrat::backends::media::DecodeError::None};
};

static_assert(musicrat::backends::media::DecoderBackend<FakeDecoder>);

} // namespace

int main() {
    using DecodeAhead = musicrat::backends::media::DecodeAhead<FakeDecoder, 3>;
    DecodeAhead decode_ahead{2, 3};

    assert(decode_ahead.open("memory:test"));
    assert(decode_ahead.last_error()
        == musicrat::backends::media::DecodeError::None);
    const auto first_generation = decode_ahead.generation();
    const auto refill = decode_ahead.refill();
    assert(refill.chunks_published == 3);
    assert(refill.state == musicrat::backends::media::DecodeAheadState::EndOfStream);

    auto final_chunk = decode_ahead.pool().try_acquire(4.0, first_generation);
    assert(final_chunk);
    assert(final_chunk.chunk.start_frame == 4);
    assert(final_chunk.chunk.audio->frame_count == 1);
    assert(final_chunk.chunk.audio->flags
        & CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM);
    assert(near(final_chunk.chunk.audio->channels[0][0], 0.4));
    assert(near(final_chunk.chunk.audio->channels[1][0], -0.4));
    decode_ahead.pool().release(final_chunk);

    assert(decode_ahead.seek(1));
    const auto second_generation = decode_ahead.generation();
    assert(second_generation != first_generation);
    assert(decode_ahead.pool().ready_count() == 0);
    assert(decode_ahead.refill().chunks_published == 2);

    auto seek_chunk = decode_ahead.pool().try_acquire(1.0, second_generation);
    assert(seek_chunk);
    assert(seek_chunk.chunk.audio->flags
        & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    decode_ahead.pool().release(seek_chunk);
}