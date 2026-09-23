#include <musicrat/dsp/stereo_panner_processor.hpp>

#include <cassert>
#include <cmath>
#include <limits>

namespace {

CommRaT::Messages::AudioBlock make_mono_block() {
    CommRaT::Messages::AudioBlock block{};
    block.sample_rate_hz = 48000.0;
    block.timestamp_ns = 1234;
    block.sequence_number = 9;
    block.frame_count = 4;
    block.channel_count = 1;
    block.flags = CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
    for (uint32_t frame = 0; frame < block.frame_count; ++frame) {
        block.channels[0].push_back(1.0);
    }
    return block;
}

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 1.0e-6;
}

} // namespace

int main() {
    musicrat::dsp::StereoPannerProcessor processor{
        CommRaT::Parameters::StereoPanner{
            .pan = -1.0,
            .pan_law = CommRaT::Parameters::PAN_LAW_LINEAR,
            .smoothing_samples = 4,
        }};

    auto input = make_mono_block();
    CommRaT::Messages::ParameterEventBlock event_block{};
    event_block.events.push_back({
        .source_endpoint_id = 1,
        .parameter_id = CommRaT::Parameters::PAN_PARAMETER_ID,
        .sample_offset = 0,
        .value = 1.0,
    });

    CommRaT::Messages::AudioBlock output{};
    processor.process(input, &event_block, output);

    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.flags == input.flags);
    assert(output.frame_count == input.frame_count);
    assert(output.channel_count == 2);
    for (uint32_t frame = 0; frame < output.frame_count; ++frame) {
        const auto right = 0.25 * static_cast<double>(frame + 1);
        assert(near(output.channels[0][frame], 1.0 - right));
        assert(near(output.channels[1][frame], right));
    }

    processor.set_parameters_immediate({
        .pan = 0.0,
        .pan_law = CommRaT::Parameters::PAN_LAW_EQUAL_POWER,
        .smoothing_samples = 0,
    });
    processor.process(input, nullptr, output);
    assert(near(output.channels[0][0], std::sqrt(0.5)));
    assert(near(output.channels[1][0], std::sqrt(0.5)));

    event_block.events[0].value = std::numeric_limits<double>::max();
    processor.process(input, &event_block, output);
    assert(near(output.channels[0][0], 0.0));
    assert(near(output.channels[1][0], 1.0));

    input.channel_count = 2;
    input.channels[1].push_back(1.0);
    input.channels[1].push_back(1.0);
    input.channels[1].push_back(1.0);
    input.channels[1].push_back(1.0);
    processor.process(input, nullptr, output);
    assert(output.frame_count == 0);
    assert(output.channel_count == 0);
    assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) != 0);
    for (const auto& channel : output.channels) {
        assert(channel.empty());
    }
}