#include <musicrat/dsp/channel_mapper_processor.hpp>

#include <cassert>
#include <cmath>

namespace {

CommRaT::Messages::AudioBlock stereo_block() {
    CommRaT::Messages::AudioBlock block{};
    block.sample_rate_hz = 48000.0;
    block.timestamp_ns = 1234;
    block.sequence_number = 9;
    block.frame_count = 2;
    block.channel_count = 2;
    block.flags = CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY;
    block.channels[0].push_back(1.0F);
    block.channels[0].push_back(0.5F);
    block.channels[1].push_back(-0.5F);
    block.channels[1].push_back(0.25F);
    return block;
}

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 1.0e-6;
}

} // namespace

int main() {
    using namespace CommRaT::Parameters;
    using musicrat::dsp::ChannelMapperConfigError;
    using musicrat::dsp::ChannelMapperProcessor;

    ChannelMapperProcessor processor;
    CommRaT::Messages::AudioBlock output{};
    auto input = stereo_block();

    assert(processor.configure({
        .mode = CHANNEL_MAP_SWAP_STEREO,
        .input_channel_count = 2,
        .output_channel_count = 2,
    }) == ChannelMapperConfigError::None);
    processor.process(input, output);
    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.flags == input.flags);
    assert(output.channel_count == 2);
    assert(near(output.channels[0][0], -0.5));
    assert(near(output.channels[1][0], 1.0));

    assert(processor.configure({
        .mode = CHANNEL_MAP_STEREO_TO_MONO,
        .input_channel_count = 2,
        .output_channel_count = 1,
    }) == ChannelMapperConfigError::None);
    processor.process(input, output);
    assert(output.channel_count == 1);
    assert(near(output.channels[0][0], 0.25));
    assert(near(output.channels[0][1], 0.375));

    assert(processor.configure({
        .mode = CHANNEL_MAP_COPY_LEFT,
        .input_channel_count = 2,
        .output_channel_count = 2,
    }) == ChannelMapperConfigError::None);
    processor.process(input, output);
    assert(near(output.channels[0][0], 1.0));
    assert(near(output.channels[1][0], 1.0));

    assert(processor.configure({
        .mode = CHANNEL_MAP_COPY_RIGHT,
        .input_channel_count = 2,
        .output_channel_count = 2,
    }) == ChannelMapperConfigError::None);
    processor.process(input, output);
    assert(near(output.channels[0][0], -0.5));
    assert(near(output.channels[1][0], -0.5));

    assert(processor.configure({
        .mode = CHANNEL_MAP_IDENTITY,
        .input_channel_count = 2,
        .output_channel_count = 2,
    }) == ChannelMapperConfigError::None);
    processor.process(input, output);
    assert(near(output.channels[0][1], 0.5));
    assert(near(output.channels[1][1], 0.25));

    ChannelMapper matrix{
        .mode = CHANNEL_MAP_MATRIX,
        .input_channel_count = 2,
        .output_channel_count = 2,
    };
    matrix.coefficients.push_back(0.5F);
    matrix.coefficients.push_back(0.5F);
    matrix.coefficients.push_back(1.0F);
    matrix.coefficients.push_back(-1.0F);
    assert(processor.configure(matrix) == ChannelMapperConfigError::None);
    processor.process(input, output);
    assert(near(output.channels[0][0], 0.25));
    assert(near(output.channels[1][0], 1.5));

    matrix.coefficients.pop_back();
    assert(processor.configure(matrix) == ChannelMapperConfigError::InvalidMatrix);
    processor.process(input, output);
    assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) != 0);
    assert(output.frame_count == 0);

    assert(processor.configure({
        .mode = CHANNEL_MAP_MONO_TO_STEREO,
        .input_channel_count = 2,
        .output_channel_count = 2,
    }) == ChannelMapperConfigError::InvalidPresetChannels);
}