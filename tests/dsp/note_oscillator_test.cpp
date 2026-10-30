#include <musicrat/dsp/note_voice_bank.hpp>
#include <musicrat/dsp/note_voice_filter.hpp>
#include <musicrat/dsp/three_oscillator_processor.hpp>
#include <musicrat/dsp/wavetable_oscillator_processor.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

namespace {

using AudioBlock = CommRaT::Messages::AudioBlock;
using NoteEventBlock = CommRaT::Messages::NoteEventBlock;

NoteEventBlock make_note_on(uint32_t sample_offset = 0) {
    NoteEventBlock events{};
    events.events.push_back({
        .source_endpoint_id = 7,
        .note_id = 42,
        .sample_offset = sample_offset,
        .type = CommRaT::Messages::NOTE_ON,
        .channel = 0,
        .key = 69,
        .value = 1.0F,
    });
    return events;
}

bool close_to(double first, double second, double tolerance = 1.0e-9) {
    return std::abs(first - second) <= tolerance;
}

void test_adsr_stages_and_release() {
    musicrat::dsp::AdsrEnvelope envelope{};
    envelope.start({
        .attack_ms = 2.0,
        .decay_ms = 2.0,
        .sustain_level = 0.5,
        .release_ms = 2.0,
    }, 1000.0);
    assert(close_to(envelope.next(), 0.0));
    assert(close_to(envelope.next(), 0.5));
    assert(close_to(envelope.next(), 1.0));
    assert(close_to(envelope.next(), 0.75));
    assert(close_to(envelope.next(), 0.5));
    assert(close_to(envelope.next(), 0.5));

    envelope.release();
    assert(close_to(envelope.next(), 0.5));
    assert(close_to(envelope.next(), 0.25));
    assert(close_to(envelope.next(), 0.0));
    assert(!envelope.active());

    envelope.start({
        .attack_ms = 0.0,
        .decay_ms = 0.0,
        .sustain_level = 0.25,
        .release_ms = 0.0,
    }, 1000.0);
    assert(close_to(envelope.next(), 0.25));
    envelope.release();
    assert(!envelope.active());
}

double filter_rms(
    uint8_t mode,
    double input_frequency,
    double resonance = 0.7071067811865476,
    double envelope = 0.0,
    double envelope_amount_octaves = 0.0,
    double cutoff_hz = 1000.0) {
    musicrat::dsp::NoteVoiceFilter filter{};
    constexpr double sample_rate_hz = 48000.0;
    double sum_squares = 0.0;
    constexpr uint32_t frames = 12000;
    constexpr uint32_t measurement_start = 2000;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const double input = std::sin(
            2.0 * std::numbers::pi * input_frequency
            * static_cast<double>(frame) / sample_rate_hz);
        const double output = filter.process(
            static_cast<AudioBlock::Sample>(input), mode, cutoff_hz,
            resonance, envelope, envelope_amount_octaves, sample_rate_hz);
        if (frame >= measurement_start) {
            sum_squares += output * output;
        }
    }
    return std::sqrt(sum_squares / (frames - measurement_start));
}

void test_filter_modes_and_resonance() {
    using namespace CommRaT::Parameters;
    assert(filter_rms(SYNTH_FILTER_LOW_PASS, 100.0)
        > filter_rms(SYNTH_FILTER_LOW_PASS, 8000.0) * 5.0);
    assert(filter_rms(SYNTH_FILTER_HIGH_PASS, 8000.0)
        > filter_rms(SYNTH_FILTER_HIGH_PASS, 100.0) * 5.0);
    assert(filter_rms(SYNTH_FILTER_BAND_PASS, 1000.0)
        > filter_rms(SYNTH_FILTER_BAND_PASS, 100.0) * 3.0);
    assert(filter_rms(SYNTH_FILTER_LOW_PASS, 1000.0, 8.0)
        > filter_rms(SYNTH_FILTER_LOW_PASS, 1000.0) * 2.0);
    assert(filter_rms(
        SYNTH_FILTER_LOW_PASS, 1500.0, 0.7071067811865476, 1.0, 2.0, 500.0)
        > filter_rms(
            SYNTH_FILTER_LOW_PASS, 1500.0, 0.7071067811865476, 0.0, 2.0, 500.0)
            * 2.0);
}

void test_voice_bank() {
    musicrat::dsp::NoteVoiceBank voices{};
    constexpr uint32_t fade_frames = 48;
    std::size_t selected = voices.voice_count;
    for (uint32_t index = 0; index < voices.voice_count + 1; ++index) {
        const CommRaT::Messages::NoteEvent event{
            .source_endpoint_id = 2,
            .note_id = index + 1,
            .sample_offset = index,
            .type = CommRaT::Messages::NOTE_ON,
            .channel = 0,
            .key = static_cast<uint8_t>(48 + index),
            .value = 0.5F,
        };
        selected = voices.apply(event, fade_frames);
    }
    assert(selected < voices.voice_count);
    assert(voices[selected].note_id == voices.voice_count + 1);
    assert(voices[selected].velocity == 0.5);
    std::size_t active_count = 0;
    for (std::size_t index = 0; index < voices.voice_count; ++index) {
        active_count += voices[index].active ? 1U : 0U;
    }
    assert(active_count == voices.voice_count);

    const CommRaT::Messages::NoteEvent release{
        .source_endpoint_id = 2,
        .note_id = static_cast<uint32_t>(voices.voice_count + 1),
        .sample_offset = 0,
        .type = CommRaT::Messages::NOTE_OFF,
        .channel = 0,
        .key = static_cast<uint8_t>(48 + voices.voice_count),
        .value = 0.0F,
    };
    const auto released_voice = voices.apply(release, fade_frames);
    assert(released_voice == voices.voice_count);
    assert(voices[selected].releasing);

    musicrat::dsp::NoteVoiceBank all_off_voices{};
    const musicrat::dsp::AdsrParameters envelope{
        .attack_ms = 0.0,
        .decay_ms = 0.0,
        .sustain_level = 1.0,
        .release_ms = 20.0,
    };
    auto channel_zero = CommRaT::Messages::NoteEvent{
        .source_endpoint_id = 9,
        .note_id = 1,
        .sample_offset = 0,
        .type = CommRaT::Messages::NOTE_ON,
        .channel = 0,
        .key = 60,
        .value = 1.0F,
    };
    auto channel_one = channel_zero;
    channel_one.note_id = 2;
    channel_one.channel = 1;
    (void)all_off_voices.apply(channel_zero, envelope, envelope, 48000.0);
    (void)all_off_voices.apply(channel_one, envelope, envelope, 48000.0);
    channel_zero.type = CommRaT::Messages::NOTE_ALL_OFF;
    (void)all_off_voices.apply(channel_zero, envelope, envelope, 48000.0);
    assert(all_off_voices[0].releasing);
    assert(!all_off_voices[1].releasing);
}

void test_three_oscillator_note_events() {
    musicrat::dsp::ThreeOscillatorProcessor processor{
        CommRaT::Parameters::ThreeOscillator{
            .sample_rate_hz = 48000.0,
            .master_gain = 1.0,
            .envelope_ms = 1.0,
            .oscillator_1_waveform = CommRaT::Parameters::SYNTH_WAVEFORM_SINE,
            .oscillator_1_semitones = 0.0,
            .oscillator_1_level = 1.0,
            .oscillator_2_waveform = CommRaT::Parameters::SYNTH_WAVEFORM_SINE,
            .oscillator_2_semitones = 0.0,
            .oscillator_2_level = 0.0,
            .oscillator_3_waveform = CommRaT::Parameters::SYNTH_WAVEFORM_SINE,
            .oscillator_3_semitones = 0.0,
            .oscillator_3_level = 0.0,
        }};
    AudioBlock output{};
    const auto note_on = make_note_on();
    processor.process(&note_on, 128, 1234, 0, output);
    assert(output.sample_rate_hz == 48000.0);
    assert(output.timestamp_ns == 1234);
    assert(output.sequence_number == 0);
    assert(output.channel_count == 1);
    assert(output.frame_count == 128);
    assert(output.channels[0][0] == 0.0);
    assert(output.channels[0][20] > 0.0);

    NoteEventBlock note_off{};
    note_off.events.push_back({
        .source_endpoint_id = 7,
        .note_id = 42,
        .sample_offset = 0,
        .type = CommRaT::Messages::NOTE_OFF,
        .channel = 0,
        .key = 69,
        .value = 0.0F,
    });
    processor.process(&note_off, 128, 5678, 1, output);
    assert(std::abs(output.channels[0].back()) < 1.0e-3);

    auto invalid = make_note_on(128);
    processor.process(&invalid, 128, 0, 2, output);
    assert(output.frame_count == 0);
    assert(output.channel_count == 0);
    assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) != 0);
}

void test_wavetable_note_input() {
    musicrat::dsp::WavetableOscillatorProcessor processor{
        CommRaT::Parameters::WavetableOscillator{
            .sample_rate_hz = 48000.0,
            .waveform = CommRaT::Parameters::SYNTH_WAVEFORM_TRIANGLE,
            .gain = 0.5,
            .envelope_ms = 1.0,
        }};
    AudioBlock output{};
    const auto note_on = make_note_on(8);
    processor.process(&note_on, 128, 4321, 5, output);
    assert(output.timestamp_ns == 4321);
    assert(output.sequence_number == 5);
    assert(output.channel_count == 1);
    assert(output.frame_count == 128);
    assert(output.channels[0][0] == 0.0);
    assert(output.channels[0][7] == 0.0);
    assert(output.channels[0][32] != 0.0);

    musicrat::dsp::WavetableOscillatorProcessor square_processor{
        CommRaT::Parameters::WavetableOscillator{
            .sample_rate_hz = 48000.0,
            .waveform = CommRaT::Parameters::SYNTH_WAVEFORM_SQUARE,
            .gain = 0.5,
            .envelope_ms = 1.0,
        }};
    AudioBlock square_output{};
    square_processor.process(&note_on, 128, 4321, 5, square_output);
    assert(std::abs(output.channels[0][64] - square_output.channels[0][64]) > 0.1);
}

template <typename Processor>
double render_note_rms(Processor& processor) {
    AudioBlock output{};
    const auto note_on = make_note_on();
    processor.process(&note_on, 256, 0, 0, output);
    double sum_squares = 0.0;
    for (uint32_t frame = 32; frame < output.frame_count; ++frame) {
        const double sample = output.channels[0][frame];
        sum_squares += sample * sample;
    }
    return std::sqrt(sum_squares / 224.0);
}

void test_synth_filter_integration() {
    auto three_parameters = CommRaT::Parameters::ThreeOscillator{};
    three_parameters.sample_rate_hz = 48000.0;
    three_parameters.master_gain = 1.0;
    three_parameters.amplitude_attack_ms = 0.0;
    three_parameters.amplitude_decay_ms = 0.0;
    three_parameters.amplitude_sustain_level = 1.0;
    three_parameters.oscillator_1_level = 1.0;
    three_parameters.oscillator_2_level = 0.0;
    three_parameters.oscillator_3_level = 0.0;
    auto three_bypass_parameters = three_parameters;
    three_parameters.filter_mode = CommRaT::Parameters::SYNTH_FILTER_HIGH_PASS;
    three_parameters.filter_cutoff_hz = 1000.0;
    musicrat::dsp::ThreeOscillatorProcessor three_bypass{
        three_bypass_parameters};
    musicrat::dsp::ThreeOscillatorProcessor three_filtered{three_parameters};
    assert(render_note_rms(three_bypass)
        > render_note_rms(three_filtered) * 2.0);

    auto wavetable_parameters = CommRaT::Parameters::WavetableOscillator{};
    wavetable_parameters.sample_rate_hz = 48000.0;
    wavetable_parameters.gain = 1.0;
    wavetable_parameters.amplitude_attack_ms = 0.0;
    wavetable_parameters.amplitude_decay_ms = 0.0;
    wavetable_parameters.amplitude_sustain_level = 1.0;
    auto wavetable_bypass_parameters = wavetable_parameters;
    wavetable_parameters.filter_mode = CommRaT::Parameters::SYNTH_FILTER_HIGH_PASS;
    wavetable_parameters.filter_cutoff_hz = 1000.0;
    musicrat::dsp::WavetableOscillatorProcessor wavetable_bypass{
        wavetable_bypass_parameters};
    musicrat::dsp::WavetableOscillatorProcessor wavetable_filtered{
        wavetable_parameters};
    assert(render_note_rms(wavetable_bypass)
        > render_note_rms(wavetable_filtered) * 2.0);
}

void test_all_notes_off_and_reset() {
    auto parameters = CommRaT::Parameters::WavetableOscillator{};
    parameters.sample_rate_hz = 48000.0;
    parameters.gain = 1.0;
    parameters.amplitude_attack_ms = 0.0;
    parameters.amplitude_decay_ms = 0.0;
    parameters.amplitude_sustain_level = 1.0;
    parameters.amplitude_release_ms = 1.0;
    musicrat::dsp::WavetableOscillatorProcessor processor{parameters};

    auto events = make_note_on();
    events.events.push_back({
        .source_endpoint_id = 7,
        .note_id = 42,
        .sample_offset = 64,
        .type = CommRaT::Messages::NOTE_ALL_OFF,
        .channel = 0,
        .key = 69,
        .value = 0.0F,
    });
    AudioBlock output{};
    processor.process(&events, 128, 0, 0, output);
    assert(std::abs(output.channels[0][63]) > 0.1);
    assert(std::abs(output.channels[0].back()) < 1.0e-6);

    processor.reset();
    processor.process(nullptr, 16, 0, 1, output);
    for (uint32_t frame = 0; frame < output.frame_count; ++frame) {
        assert(output.channels[0][frame] == 0.0);
    }
}

} // namespace

void test_three_oscillator_automation() {
    musicrat::dsp::ThreeOscillatorProcessor processor;
    auto notes = make_note_on();
    CommRaT::Messages::ParameterEventBlock events;
    events.events.push_back({ .parameter_id = 2, .sample_offset = 64, .value = 0.0 });
    AudioBlock output;
    processor.process(&notes, &events, 128, 42, 7, output);
    assert(output.timestamp_ns == 42 && output.sequence_number == 7);
    assert(std::abs(output.channels[0][63]) > 0.0);
    for (uint32_t frame = 64; frame < 128; ++frame) assert(output.channels[0][frame] == 0);
    assert(processor.parameters().master_gain == 0);
    musicrat::dsp::visit_three_oscillator_parameters([&](uint32_t id, auto member) {
        events.events.clear();
        events.events.push_back({ .parameter_id = id, .value = 0.5 });
        processor.process(nullptr, &events, 16, 0, 0, output);
        assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) == 0);
        assert(static_cast<double>(processor.parameters().*member) > 0);
    });
    const auto gain = processor.parameters().master_gain;
    for (const auto& invalid : {
        CommRaT::Messages::ParameterEvent{ .parameter_id = 1, .value = 96000 },
        CommRaT::Messages::ParameterEvent{ .parameter_id = 25, .value = 1 },
        CommRaT::Messages::ParameterEvent{ .parameter_id = 2, .sample_offset = 16, .value = 1 },
        CommRaT::Messages::ParameterEvent{ .parameter_id = 2, .value = std::nan("") },
    }) {
        events.events.clear();
        events.events.push_back(invalid);
        processor.process(nullptr, &events, 16, 0, 0, output);
        assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) != 0);
        assert(processor.parameters().master_gain == gain);
        assert(processor.parameters().sample_rate_hz == musicrat::config::default_sample_rate_hz);
    }
}

int main() {
    test_three_oscillator_automation();
    test_adsr_stages_and_release();
    test_filter_modes_and_resonance();
    test_voice_bank();
    test_three_oscillator_note_events();
    test_wavetable_note_input();
    test_synth_filter_integration();
    test_all_notes_off_and_reset();
}
