#include <musicrat/dsp/biquad_filter_processor.hpp>
#include <musicrat/dsp/dc_blocker_processor.hpp>
#include <musicrat/dsp/parametric_equalizer_processor.hpp>

#include <cassert>
#include <cmath>
#include <numbers>

namespace {

using AudioBlock = CommRaT::Messages::AudioBlock;

AudioBlock make_block(uint32_t frames, double value = 1.0) {
    AudioBlock block{};
    block.sample_rate_hz = 48000.0;
    block.timestamp_ns = 1234;
    block.sequence_number = 9;
    block.frame_count = frames;
    block.channel_count = 1;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        block.channels[0].push_back(static_cast<AudioBlock::Sample>(value));
    }
    return block;
}

bool near(double actual, double expected, double tolerance = 1.0e-5) {
    return std::abs(actual - expected) <= tolerance;
}

void test_dc_blocker() {
    musicrat::dsp::DcBlockerProcessor processor;
    AudioBlock output{};
    for (int block_index = 0; block_index < 6; ++block_index) {
        const auto input = make_block(1024);
        processor.process(input, output);
        assert(output.sample_rate_hz == input.sample_rate_hz);
        assert(output.timestamp_ns == input.timestamp_ns);
        assert(output.sequence_number == input.sequence_number);
        assert(output.frame_count == input.frame_count);
        assert(output.channel_count == input.channel_count);
        assert(output.channels[0].size() == input.frame_count);
    }
    assert(std::abs(output.channels[0].back()) < 1.0e-5);

    CommRaT::Parameters::DcBlocker parameters{.cutoff_hz = 10.0};
    processor.set_parameters(parameters);
    auto discontinuity = make_block(2, 0.5);
    discontinuity.flags = CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
    processor.process(discontinuity, output);
    assert(near(output.channels[0][0], 0.5));

    auto invalid = make_block(2);
    invalid.channels[0].pop_back();
    processor.process(invalid, output);
    assert(output.frame_count == 0);
    assert(output.channel_count == 0);
    assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) != 0);
}

void test_biquad_filter() {
    musicrat::dsp::BiquadFilterProcessor low_pass{
        CommRaT::Parameters::BiquadFilter{
            .filter_type = CommRaT::Parameters::BIQUAD_FILTER_LOW_PASS,
            .frequency_hz = 1000.0,
            .q = 0.7071067811865476,
            .gain_db = 0.0,
        }};
    AudioBlock output{};
    const auto step = make_block(1024);
    low_pass.process(step, output);
    assert(output.channels[0].size() == step.frame_count);
    assert(near(output.channels[0].back(), 1.0, 1.0e-3));
    assert(output.timestamp_ns == step.timestamp_ns);
    assert(output.flags == step.flags);

    musicrat::dsp::BiquadFilterProcessor high_pass{
        CommRaT::Parameters::BiquadFilter{
            .filter_type = CommRaT::Parameters::BIQUAD_FILTER_HIGH_PASS,
            .frequency_hz = 1000.0,
            .q = 0.7071067811865476,
            .gain_db = 0.0,
        }};
    high_pass.process(step, output);
    assert(std::abs(output.channels[0].back()) < 1.0e-3);

    const auto coefficients = musicrat::dsp::make_biquad_coefficients(
        musicrat::dsp::BiquadType::Notch,
        48000.0, 1000.0, 0.7071067811865476, 0.0);
    for (const double coefficient : {
             coefficients.b0, coefficients.b1, coefficients.b2,
             coefficients.a1, coefficients.a2}) {
        assert(std::isfinite(coefficient));
    }
}

void test_three_band_equalizer() {
    musicrat::dsp::ParametricEqualizerProcessor equalizer;
    AudioBlock output{};
    auto input = make_block(1024, 0.25);
    equalizer.process(input, output);
    for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
        assert(near(output.channels[0][frame], input.channels[0][frame]));
    }

    constexpr double amplitude = 0.1;
    constexpr double frequency_hz = 1000.0;
    input = {};
    input.sample_rate_hz = 48000.0;
    input.frame_count = 2048;
    input.channel_count = 1;
    for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
        const double phase = 2.0 * std::numbers::pi * frequency_hz
            * static_cast<double>(frame) / input.sample_rate_hz;
        input.channels[0].push_back(
            static_cast<AudioBlock::Sample>(amplitude * std::sin(phase)));
    }
    equalizer.set_parameters({
        .low_shelf_frequency_hz = 120.0,
        .low_shelf_gain_db = 0.0,
        .mid_frequency_hz = frequency_hz,
        .mid_gain_db = 6.0,
        .mid_q = 1.0,
        .high_shelf_frequency_hz = 8000.0,
        .high_shelf_gain_db = 0.0,
    });
    equalizer.process(input, output);
    double input_energy = 0.0;
    double output_energy = 0.0;
    for (uint32_t frame = 1024; frame < input.frame_count; ++frame) {
        const double in = input.channels[0][frame];
        const double out = output.channels[0][frame];
        input_energy += in * in;
        output_energy += out * out;
    }
    const double rms_ratio = std::sqrt(output_energy / input_energy);
    assert(near(rms_ratio, std::pow(10.0, 6.0 / 20.0), 0.02));
}

void test_equalizer_parameter_events() {
    musicrat::dsp::ParametricEqualizerProcessor equalizer;
    AudioBlock input{};
    input.sample_rate_hz = 48000.0;
    input.frame_count = 2048;
    input.channel_count = 1;
    for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
        const double phase = 2.0 * std::numbers::pi * 1000.0
            * static_cast<double>(frame) / input.sample_rate_hz;
        input.channels[0].push_back(
            static_cast<AudioBlock::Sample>(0.1 * std::sin(phase)));
    }

    CommRaT::Messages::ParameterEventBlock events{};
    events.events.push_back({
        .parameter_id =
            CommRaT::Parameters::PARAMETRIC_EQ_MID_GAIN_PARAMETER_ID,
        .sample_offset = 1024,
        .value = 6.0,
    });
    AudioBlock baseline{};
    AudioBlock output{};
    equalizer.process(input, baseline);
    equalizer.process(input, &events, output);

    double input_energy = 0.0;
    double output_energy = 0.0;
    for (uint32_t frame = 0; frame < 1024; ++frame) {
        assert(near(output.channels[0][frame], baseline.channels[0][frame]));
    }
    for (uint32_t frame = 1024; frame < input.frame_count; ++frame) {
        const double in = input.channels[0][frame];
        const double out = output.channels[0][frame];
        input_energy += in * in;
        output_energy += out * out;
    }
    const double rms_ratio = std::sqrt(output_energy / input_energy);
    assert(near(rms_ratio, std::pow(10.0, 6.0 / 20.0), 0.02));

    events.events.clear();
    events.events.push_back({
        .parameter_id = 999,
        .value = 24.0,
    });
    equalizer.process(input, &events, output);
    assert(output.channels[0].size() == input.frame_count);
}

} // namespace

int main() {
    test_dc_blocker();
    test_biquad_filter();
    test_three_band_equalizer();
    test_equalizer_parameter_events();
}
