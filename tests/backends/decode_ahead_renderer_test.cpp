#include <musicrat/backends/audio/wav_writer.hpp>
#include <musicrat/backends/media/decode_ahead_renderer.hpp>
#include <musicrat/backends/media/wav_decode_ahead.hpp>

#include <cassert>
#include <cmath>
#include <filesystem>

namespace {

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.0001;
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path()
        / "musicrat_decode_ahead_renderer_test.wav";
#if MUSICRAT_HAS_RUBBERBAND
    const auto pitch_path = std::filesystem::temp_directory_path()
        / "musicrat_pitch_lock_decode_ahead_renderer_test.wav";
#endif

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
    assert(decode_ahead.refill().chunks_published == 3);

    musicrat::backends::media::DecodeAheadRenderer<3> renderer{
        decode_ahead.pool()};
    renderer.prepare(decode_ahead.generation(), 0.0);

    CommRaT::Messages::AudioBlock output{};
    const auto result = renderer.render(48000.0, 6, 1000, 1, output);
    assert(result.source_frames_rendered == 6);
    assert(result.silent_frames == 0);
    assert(near(result.media_position, 6.0));
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    for (uint32_t frame = 0; frame < 6; ++frame) {
        assert(near(output.channels[0][frame], static_cast<double>(frame) / 10.0));
    }
    assert(decode_ahead.pool().ready_count() == 0);

    assert(decode_ahead.seek(2));
    renderer.prepare(decode_ahead.generation(), 2.0);
    CommRaT::Messages::DeckControlEventBlock controls{};
    controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_RATE,
        .sample_offset = 1,
        .value = 2.0,
    });
    const auto unavailable = renderer.render(
        48000.0, 2, 2000, 2, output, &controls);
    assert(unavailable.silent_frames == 2);
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_UNDERRUN);
    assert(near(renderer.media_position(), 2.0));

    assert(decode_ahead.refill().chunks_published == 2);
    const auto after_refill = renderer.render(48000.0, 2, 3000, 3, output);
    assert(after_refill.source_frames_rendered == 2);
    assert(after_refill.silent_frames == 0);
    assert(near(output.channels[0][0], 0.2));
    assert(near(output.channels[0][1], 0.4));
    assert(near(renderer.media_position(), 6.0));

    assert(decode_ahead.seek(2));
    renderer.prepare(decode_ahead.generation(), 2.0);
    assert(decode_ahead.refill().chunks_published == 2);
    CommRaT::Messages::DeckControlEventBlock loop_controls{};
    loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_RATE,
        .sample_offset = 0,
        .value = 1.0,
    });
    loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_LOOP_START,
        .sample_offset = 0,
        .value = 2.0,
    });
    loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_LOOP_END,
        .sample_offset = 0,
        .value = 6.0,
    });
    loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_ENABLE_LOOP,
        .sample_offset = 0,
    });
    const auto looped = renderer.render(
        48000.0, 5, 4000, 4, output, &loop_controls);
    assert(looped.source_frames_rendered == 5);
    assert(renderer.loop_enabled());
    assert(near(renderer.media_position(), 3.0));
    assert(near(output.channels[0][0], 0.2));
    assert(near(output.channels[0][3], 0.5));
    assert(near(output.channels[0][4], 0.2));

    const auto retained_loop = renderer.render(48000.0, 4, 5000, 5, output);
    assert(retained_loop.source_frames_rendered == 4);
    assert(retained_loop.silent_frames == 0);
    assert(near(output.channels[0][0], 0.3));
    assert(near(output.channels[0][1], 0.4));
    assert(near(output.channels[0][2], 0.5));
    assert(near(output.channels[0][3], 0.2));

#if MUSICRAT_HAS_RUBBERBAND
    CommRaT::Messages::AudioBlock pitch_source{};
    pitch_source.sample_rate_hz = 48000.0;
    pitch_source.frame_count = CommRaT::Messages::AudioBlock::MAX_FRAMES;
    pitch_source.channel_count = 1;
    constexpr double two_pi = 6.28318530717958647692;
    for (uint32_t frame = 0; frame < pitch_source.frame_count; ++frame) {
        pitch_source.channels[0].push_back(static_cast<
            CommRaT::Messages::AudioBlock::Sample>(
            0.25 * std::sin(two_pi * 440.0 * frame / 48000.0)));
    }
    {
        musicrat::backends::audio::WavWriter writer{};
        assert(writer.open(pitch_path.c_str(), 48000, 1));
        assert(writer.write(pitch_source));
        assert(writer.write(pitch_source));
    }

    DecodeAhead pitch_decode_ahead{4096, 2};
    assert(pitch_decode_ahead.open(pitch_path.c_str()));
    assert(pitch_decode_ahead.refill().chunks_published == 2);
    musicrat::backends::media::DecodeAheadRenderer<3> pitch_renderer{
        pitch_decode_ahead.pool(), 48000.0, true};
    pitch_renderer.prepare(pitch_decode_ahead.generation(), 0.0);
    assert(pitch_renderer.pitch_lock_enabled());
    assert(pitch_renderer.algorithmic_latency_frames() > 0);

    CommRaT::Messages::DeckControlEventBlock pitch_controls{};
    pitch_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_CUE,
        .sample_offset = 0,
        .value = 64.0,
    });
    pitch_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_LOOP_START,
        .sample_offset = 0,
        .value = 0.0,
    });
    pitch_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_LOOP_END,
        .sample_offset = 0,
        .value = 1024.0,
    });
    pitch_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_ENABLE_LOOP,
        .sample_offset = 0,
    });
    pitch_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_RATE,
        .sample_offset = 128,
        .value = 1.5,
    });
    pitch_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_RAMP_RATE,
        .sample_offset = 256,
        .value = 2.0,
        .ramp_frames = 128,
    });
    const auto automated = pitch_renderer.render(
        48000.0, 512, 6000, 6, output, &pitch_controls);
    assert(automated.source_frames_rendered == 512);
    assert(near(pitch_renderer.current_rate(), 2.0));
    assert(near(pitch_renderer.media_position(), 800.25));
    assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) == 0);

    CommRaT::Messages::DeckControlEventBlock return_controls{};
    return_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_RETURN_TO_CUE,
        .sample_offset = 64,
    });
    const auto returned = pitch_renderer.render(
        48000.0, 512, 7000, 7, output, &return_controls);
    assert(returned.source_frames_rendered == 512);
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) == 0);

    assert(pitch_decode_ahead.seek(2048));
    pitch_renderer.prepare(pitch_decode_ahead.generation(), 2048.0);
    assert(pitch_decode_ahead.refill().chunks_published == 2);
    CommRaT::Messages::DeckControlEventBlock invalid_rate_controls{};
    invalid_rate_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_RATE,
        .sample_offset = 0,
        .value = 4.0,
    });
    const auto after_pitch_seek = pitch_renderer.render(
        48000.0, 512, 8000, 8, output, &invalid_rate_controls);
    assert(after_pitch_seek.source_frames_rendered == 512);
    assert(near(pitch_renderer.current_rate(), 2.0));
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) == 0);
#endif

    std::filesystem::remove(path);
#if MUSICRAT_HAS_RUBBERBAND
    std::filesystem::remove(pitch_path);
#endif
}