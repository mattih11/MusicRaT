#include <musicrat/dsp/level_meter_processor.hpp>

#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using AudioBlock = CommRaT::Messages::AudioBlock;
    using Sample = AudioBlock::Sample;

    AudioBlock input{};
    input.sample_rate_hz = 48000.0;
    input.timestamp_ns = 1234;
    input.sequence_number = 8;
    input.frame_count = 4;
    input.channel_count = 1;
    input.channels[0].push_back(Sample{-1});
    input.channels[0].push_back(Sample{-0.5});
    input.channels[0].push_back(Sample{0.5});
    input.channels[0].push_back(Sample{1});

    musicrat::dsp::LevelMeterProcessor processor{
        CommRaT::Parameters::LevelMeter{.clipping_threshold = Sample{0.9}}};
    AudioBlock output{};
    CommRaT::Messages::LevelMeterBlock meter{};
    processor.process(input, output, meter);

    assert(output.channels[0] == input.channels[0]);
    assert(output.timestamp_ns == input.timestamp_ns);
    assert(meter.timestamp_ns == input.timestamp_ns);
    assert(meter.sequence_number == input.sequence_number);
    assert(meter.channel_count == 1);
    assert(meter.peak[0] == Sample{1});
    assert(std::abs(meter.rms[0] - static_cast<Sample>(std::sqrt(0.625)))
        < Sample{1.0e-6});
    assert(meter.clipped[0] == 1);

    input.channels[0][0] = std::numeric_limits<Sample>::quiet_NaN();
    processor.process(input, output, meter);
    assert((output.flags & CommRaT::Messages::AUDIO_BLOCK_INVALID) != 0);
    assert((meter.flags & CommRaT::Messages::LEVEL_METER_INVALID) != 0);
    assert(output.frame_count == 0);
    assert(meter.channel_count == 0);
}