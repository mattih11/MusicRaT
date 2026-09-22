#include <musicrat/dsp/varispeed_renderer.hpp>

#include <cassert>
#include <cmath>

namespace {

using AudioBlock = CommRaT::Messages::AudioBlock;

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 1.0e-6;
}

AudioBlock make_source() {
    AudioBlock source{};
    source.sample_rate_hz = 48000.0;
    source.frame_count = 4;
    source.channel_count = 1;
    source.flags = CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
    source.channels[0].push_back(0.0);
    source.channels[0].push_back(1.0);
    source.channels[0].push_back(0.0);
    source.channels[0].push_back(-1.0);
    return source;
}

AudioBlock make_loop_source() {
    AudioBlock source{};
    source.sample_rate_hz = 48000.0;
    source.frame_count = 10;
    source.channel_count = 1;
    source.flags = CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM;
    for (uint32_t frame = 0; frame < source.frame_count; ++frame) {
        source.channels[0].push_back(static_cast<AudioBlock::Sample>(frame));
    }
    return source;
}

} // namespace

int main() {
    auto source = make_source();
    const musicrat::dsp::DecodedAudioChunkView chunk{
        .audio = &source,
        .start_frame = 100,
        .generation = 7,
    };

    musicrat::dsp::VarispeedRenderer renderer{};
    assert(renderer.set_rate_bounds(0.5, 2.0));
    assert(!renderer.set_rate_immediate(0.25));
    renderer.prepare(7, 100.0);
    assert(renderer.set_rate_immediate(0.5));
    assert(!renderer.set_rate_immediate(0.0));

    AudioBlock output{};
    auto result = renderer.render(chunk, 48000.0, 4, 1234, 9, output);
    assert(result.source_frames_rendered == 4);
    assert(result.silent_frames == 0);
    assert(near(result.media_position, 102.0));
    assert(output.sample_rate_hz == 48000.0);
    assert(output.timestamp_ns == 1234);
    assert(output.sequence_number == 9);
    assert(output.frame_count == 4);
    assert(output.channel_count == 1);
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    assert(near(output.channels[0][0], 0.0));
    assert(near(output.channels[0][1], 0.5));
    assert(near(output.channels[0][2], 1.0));
    assert(near(output.channels[0][3], 0.5));

    renderer.set_playing(false);
    result = renderer.render(chunk, 48000.0, 2, 2000, 10, output);
    assert(result.silent_frames == 2);
    assert(near(result.media_position, 102.0));
    assert(output.flags == CommRaT::Messages::AUDIO_BLOCK_SILENCE);
    assert(near(output.channels[0][0], 0.0));

    renderer.set_playing(true);
    renderer.prepare(8, 100.0);
    result = renderer.render(chunk, 48000.0, 2, 3000, 11, output);
    assert(result.generation_mismatch);
    assert(result.silent_frames == 2);
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_UNDERRUN);
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_SILENCE);

    renderer.prepare(7, 103.0);
    assert(renderer.set_rate_immediate(1.0));
    result = renderer.render(chunk, 48000.0, 3, 4000, 12, output);
    assert(result.source_frames_rendered == 1);
    assert(result.silent_frames == 2);
    assert(result.reached_end);
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM);
    assert(near(output.channels[0][0], -1.0));
    assert(near(output.channels[0][1], 0.0));

    renderer.prepare(7, 103.5);
    assert(renderer.set_rate_immediate(0.5));
    result = renderer.render(chunk, 48000.0, 1, 4500, 13, output);
    assert(result.source_frames_rendered == 1);
    assert(result.silent_frames == 0);
    assert(near(output.channels[0][0], -1.0));

    renderer.prepare(7, 100.0);
    assert(renderer.set_rate_immediate(1.0));
    assert(renderer.ramp_rate(2.0, 2));
    std::array<double, 2> rendered_rates{};
    result = renderer.render(
        chunk, 96000.0, 2, 5000, 14, output, nullptr, rendered_rates);
    assert(near(result.media_position, 101.75));
    assert(near(renderer.current_rate(), 2.0));
    assert(near(rendered_rates[0], 1.5));
    assert(near(rendered_rates[1], 2.0));

    renderer.prepare(7, 100.0);
    assert(renderer.set_rate_immediate(1.0));
    CommRaT::Messages::DeckControlEventBlock controls{};
    controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_PAUSE,
        .sample_offset = 1,
    });
    controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_PLAY,
        .sample_offset = 2,
    });
    controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_RATE,
        .sample_offset = 3,
        .value = 2.0,
    });
    result = renderer.render(chunk, 48000.0, 4, 6000, 15, output, &controls);
    assert(result.source_frames_rendered == 3);
    assert(result.silent_frames == 1);
    assert(near(result.media_position, 104.0));
    assert(near(output.channels[0][0], 0.0));
    assert(near(output.channels[0][1], 0.0));
    assert(near(output.channels[0][2], 1.0));
    assert(near(output.channels[0][3], 0.0));

    auto loop_source = make_loop_source();
    const musicrat::dsp::DecodedAudioChunkView loop_chunk{
        .audio = &loop_source,
        .start_frame = 0,
        .generation = 9,
    };
    renderer.prepare(9, 0.0);
    assert(renderer.set_rate_immediate(1.0));
    CommRaT::Messages::DeckControlEventBlock loop_controls{};
    loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_CUE,
        .sample_offset = 0,
        .value = 3.0,
    });
    loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_LOOP_START,
        .sample_offset = 0,
        .value = 2.0,
    });
    loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_LOOP_END,
        .sample_offset = 0,
        .value = 8.0,
    });
    loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_ENABLE_LOOP,
        .sample_offset = 0,
    });
    result = renderer.render(
        loop_chunk, 48000.0, 10, 7000, 16, output, &loop_controls);
    assert(renderer.loop_enabled());
    assert(near(renderer.retention_floor(), 2.0));
    assert(near(result.media_position, 4.0));
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    for (uint32_t frame = 0; frame < 8; ++frame) {
        assert(near(output.channels[0][frame], static_cast<double>(frame)));
    }
    assert(near(output.channels[0][8], 2.0));
    assert(near(output.channels[0][9], 3.0));

    CommRaT::Messages::DeckControlEventBlock cue_controls{};
    cue_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_RETURN_TO_CUE,
        .sample_offset = 1,
    });
    result = renderer.render(
        loop_chunk, 48000.0, 3, 8000, 17, output, &cue_controls);
    assert(near(output.channels[0][0], 4.0));
    assert(near(output.channels[0][1], 3.0));
    assert(near(output.channels[0][2], 4.0));
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);

    renderer.prepare(9, 0.0);
    CommRaT::Messages::DeckControlEventBlock invalid_loop_controls{};
    invalid_loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_LOOP_START,
        .sample_offset = 0,
        .value = 8.0,
    });
    invalid_loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_SET_LOOP_END,
        .sample_offset = 0,
        .value = 12.0,
    });
    invalid_loop_controls.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_ENABLE_LOOP,
        .sample_offset = 0,
    });
    renderer.render(
        loop_chunk, 48000.0, 1, 9000, 18, output, &invalid_loop_controls);
    assert(!renderer.loop_enabled());
}