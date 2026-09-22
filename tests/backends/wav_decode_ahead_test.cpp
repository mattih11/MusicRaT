#include <musicrat/backends/audio/wav_writer.hpp>
#include <musicrat/backends/media/wav_decode_ahead.hpp>

#include <cassert>
#include <filesystem>

int main() {
    const auto path = std::filesystem::temp_directory_path()
        / "musicrat_wav_decode_ahead_test.wav";

    CommRaT::Messages::AudioBlock source{};
    source.sample_rate_hz = 48000.0;
    source.frame_count = 6;
    source.channel_count = 1;
    for (uint32_t frame = 0; frame < source.frame_count; ++frame) {
        source.channels[0].push_back(static_cast<double>(frame) / 10.0);
    }
    {
        musicrat::backends::audio::WavWriter writer{};
        assert(writer.open(path.c_str(), 48000, 1));
        assert(writer.write(source));
    }

    using DecodeAhead = musicrat::backends::media::WavDecodeAhead<3>;
    DecodeAhead decode_ahead{2, 3};
    assert(decode_ahead.open(path.c_str()));
    const uint64_t initial_generation = decode_ahead.generation();
    assert(initial_generation != 0);

    const auto initial_refill = decode_ahead.refill();
    assert(initial_refill.chunks_published == 3);
    assert(initial_refill.state == musicrat::backends::media::DecodeAheadState::EndOfStream);
    assert(decode_ahead.pool().ready_count() == 3);

    auto middle = decode_ahead.pool().try_acquire(2.5, initial_generation);
    assert(middle);
    assert(middle.chunk.start_frame == 2);
    assert(middle.chunk.audio->frame_count == 2);
    assert(middle.chunk.audio->channels[0][0] > 0.19);
    decode_ahead.pool().release(middle);

    assert(decode_ahead.seek(1));
    const uint64_t seek_generation = decode_ahead.generation();
    assert(seek_generation != initial_generation);
    assert(decode_ahead.pool().ready_count() == 0);
    assert(!decode_ahead.pool().try_acquire(2.0, initial_generation));

    const auto seek_refill = decode_ahead.refill();
    assert(seek_refill.chunks_published == 3);
    auto after_seek = decode_ahead.pool().try_acquire(1.0, seek_generation);
    assert(after_seek);
    assert(after_seek.chunk.start_frame == 1);
    assert(after_seek.chunk.audio->flags
        & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    decode_ahead.pool().release(after_seek);

    decode_ahead.close();
    assert(decode_ahead.state() == musicrat::backends::media::DecodeAheadState::Closed);
    assert(decode_ahead.pool().ready_count() == 0);
    std::filesystem::remove(path);
}