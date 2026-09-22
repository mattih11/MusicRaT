#include <musicrat/dsp/gain_processor.hpp>

#include <cassert>
#include <cmath>

namespace {

CommRaT::Messages::AudioBlock make_stereo_block() {
    CommRaT::Messages::AudioBlock block{};
    block.sample_rate_hz = 48000.0;
    block.timestamp_ns = 1234;
    block.sequence_number = 9;
    block.frame_count = 4;
    block.channel_count = 2;
    block.flags = CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
    for (uint32_t frame = 0; frame < block.frame_count; ++frame) {
        block.channels[0].push_back(1.0);
        block.channels[1].push_back(1.0);
    }
    return block;
}

} // namespace

int main() {
    musicrat::dsp::GainProcessor processor{CommRaT::Parameters::Gain{
        .gain = 1.0,
        .smoothing_samples = 4,
        .muted = false,
        .invert_polarity = false,
    }};

    auto input = make_stereo_block();
    CommRaT::Messages::ParameterEventBlock event_block{};
    event_block.events.push_back({
        .source_endpoint_id = 1,
        .parameter_id = CommRaT::Parameters::GAIN_PARAMETER_ID,
        .sample_offset = 0,
        .value = 0.0,
    });

    CommRaT::Messages::AudioBlock output{};
    processor.process(input, &event_block, output);

    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.flags == input.flags);
    assert(output.channel_count == 2);
    assert(output.frame_count == 4);
    for (uint32_t frame = 0; frame < output.frame_count; ++frame) {
        const auto expected = 0.75 - 0.25 * static_cast<double>(frame);
        assert(std::abs(output.channels[0][frame] - expected) < 1.0e-6);
        assert(output.channels[0][frame] == output.channels[1][frame]);
    }

    input.channel_count = 1;
    processor.process(input, nullptr, output);
    assert(output.frame_count == 0);
    assert(output.channel_count == 0);
    assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) != 0);
    for (const auto& channel : output.channels) {
        assert(channel.empty());
    }

    input = make_stereo_block();
    processor.set_parameters({
        .gain = 1.0,
        .smoothing_samples = 0,
        .muted = true,
        .invert_polarity = false,
    });
    processor.process(input, nullptr, output);
    assert(output.channels[0][0] == 0.0);

    processor.set_parameters({
        .gain = 0.5,
        .smoothing_samples = 0,
        .muted = false,
        .invert_polarity = true,
    });
    processor.process(input, nullptr, output);
    assert(output.channels[0][0] == -0.5);
}