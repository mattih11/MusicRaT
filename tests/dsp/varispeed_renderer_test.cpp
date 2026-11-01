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
    assert((renderer.markers() == CommRaT::Messages::DeckMarkerState{
        .cue_frame = 3, .loop_start_frame = 2, .loop_end_frame = 8,
        .cue_set = true, .loop_start_set = true, .loop_end_set = true, .loop_enabled = true}));
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
    assert(renderer.markers().cue_set && renderer.markers().cue_frame == 3);
    assert(renderer.markers().loop_start_set && renderer.markers().loop_end_set);
    assert(renderer.markers().loop_end_frame == 12 && !renderer.markers().loop_enabled);
    renderer.clear_track_controls();
    assert(renderer.markers() == CommRaT::Messages::DeckMarkerState{});
    renderer.set_playing(false);
    CommRaT::Messages::DeckControlEventBlock partial{};
    partial.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_SET_CUE, .value = 0});
    partial.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_SET_LOOP_END, .value = 8});
    renderer.render(loop_chunk, 48000, 1, 10000, 19, output, &partial);
    assert(renderer.markers().cue_set && renderer.markers().cue_frame == 0);
    assert(!renderer.markers().loop_start_set && renderer.markers().loop_end_set);
    partial.events.clear();
    partial.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_ENABLE_LOOP});
    renderer.render(loop_chunk, 48000, 1, 10000, 20, output, &partial);
    assert(renderer.markers().loop_start_set && renderer.markers().loop_enabled);
    partial.events.clear();
    partial.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CLEAR_CUE});
    partial.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_DISABLE_LOOP});
    renderer.render(loop_chunk, 48000, 1, 10000, 21, output, &partial);
    assert(!renderer.markers().cue_set && !renderer.markers().loop_enabled);
    assert(renderer.markers().loop_start_set && renderer.markers().loop_end_set);

    renderer.clear_track_controls();
    renderer.prepare(9, 3.0);
    renderer.set_playing(false);
    CommRaT::Messages::DeckControlEventBlock cue_button{};
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_DOWN});
    renderer.render(loop_chunk, 48000, 1, 11000, 22, output, &cue_button);
    assert(!renderer.playing() && renderer.markers().cue_set && renderer.markers().cue_frame == 3);

    renderer.prepare(9, 6.0);
    renderer.set_playing(true);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_DOWN});
    renderer.render(loop_chunk, 48000, 1, 12000, 23, output, &cue_button);
    assert(!renderer.playing() && renderer.media_position() == 3.0);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_HOLD});
    renderer.render(loop_chunk, 48000, 1, 12500, 24, output, &cue_button);
    assert(renderer.playing() && renderer.media_position() == 4.0);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_UP});
    renderer.render(loop_chunk, 48000, 1, 12750, 25, output, &cue_button);
    assert(!renderer.playing() && renderer.media_position() == 3.0);

    renderer.prepare(9, 3.0);
    renderer.set_playing(false);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_DOWN});
    renderer.render(loop_chunk, 48000, 2, 13000, 26, output, &cue_button);
    assert(renderer.playing() && renderer.media_position() == 5.0);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_UP});
    renderer.render(loop_chunk, 48000, 1, 14000, 27, output, &cue_button);
    assert(!renderer.playing() && renderer.media_position() == 3.0);

    renderer.prepare(9, 7.0);
    renderer.set_playing(false);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_DOWN});
    renderer.render(loop_chunk, 48000, 1, 15000, 28, output, &cue_button);
    assert(!renderer.playing() && renderer.markers().cue_frame == 7);

    // Decode-ahead has released the cue chunk: every cue return must request a reseek.
    const musicrat::dsp::DecodedAudioChunkView late_chunk{
        .audio = &loop_source,
        .start_frame = 100,
        .generation = 9,
    };
    uint64_t seek_frame = 0;
    assert(!renderer.take_seek_request(seek_frame));
    renderer.prepare(9, 102.0);
    renderer.set_playing(true);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_DOWN});
    renderer.render(late_chunk, 48000, 1, 16000, 29, output, &cue_button);
    assert(!renderer.playing());
    assert(renderer.take_seek_request(seek_frame) && seek_frame == 7);
    assert(!renderer.take_seek_request(seek_frame));
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_UP});
    renderer.render(late_chunk, 48000, 1, 16100, 30, output, &cue_button);
    assert(!renderer.playing());
    renderer.prepare(9, 7.0);
    assert(!renderer.playing() && renderer.media_position() == 7.0);

    // Hold while playing: HOLD before the reseek lands defers preview until the cue is ready.
    renderer.prepare(9, 102.0);
    renderer.set_playing(true);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_DOWN});
    renderer.render(late_chunk, 48000, 1, 17000, 31, output, &cue_button);
    assert(renderer.take_seek_request(seek_frame) && seek_frame == 7);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_HOLD});
    renderer.render(late_chunk, 48000, 1, 17100, 32, output, &cue_button);
    assert(!renderer.playing());
    renderer.prepare(9, 7.0);
    assert(renderer.playing() && renderer.media_position() == 7.0);
    const musicrat::dsp::DecodedAudioChunkView cue_chunk{
        .audio = &loop_source,
        .start_frame = 5,
        .generation = 9,
    };
    renderer.render(cue_chunk, 48000, 2, 17200, 33, output);
    assert(renderer.playing() && renderer.media_position() == 9.0);

    // Preview played past the resident window: release pauses and reseeks to the cue.
    const musicrat::dsp::DecodedAudioChunkView later_chunk{
        .audio = &loop_source,
        .start_frame = 200,
        .generation = 9,
    };
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_UP});
    renderer.render(later_chunk, 48000, 1, 17300, 34, output, &cue_button);
    assert(!renderer.playing());
    assert(renderer.take_seek_request(seek_frame) && seek_frame == 7);
    renderer.prepare(9, 7.0);
    assert(!renderer.playing() && renderer.media_position() == 7.0);

    // Hold while paused at the cue, then release after the cue chunk was evicted.
    renderer.render(cue_chunk, 48000, 1, 18000, 35, output);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_DOWN});
    renderer.render(cue_chunk, 48000, 1, 18100, 36, output, &cue_button);
    assert(renderer.playing() && renderer.media_position() == 8.0);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_UP});
    renderer.render(later_chunk, 48000, 1, 18200, 37, output, &cue_button);
    assert(!renderer.playing());
    assert(renderer.take_seek_request(seek_frame) && seek_frame == 7);
    renderer.prepare(9, 7.0);
    assert(!renderer.playing() && renderer.media_position() == 7.0);

    // An unrelated seek cancels the in-flight cue gesture.
    renderer.prepare(9, 102.0);
    renderer.set_playing(true);
    cue_button.events.clear();
    cue_button.events.push_back({.type = CommRaT::Messages::DECK_CONTROL_CUE_BUTTON_DOWN});
    renderer.render(late_chunk, 48000, 1, 19000, 38, output, &cue_button);
    assert(renderer.take_seek_request(seek_frame));
    renderer.prepare(9, 50.0);
    assert(!renderer.playing() && renderer.media_position() == 50.0);
}