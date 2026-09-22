#include <musicrat/dsp/pitch_lock_processor.hpp>

#include <cassert>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <new>

namespace {

std::atomic<bool> track_allocations{false};
std::atomic<uint64_t> allocation_count{0};

} // namespace

void* operator new(std::size_t size) {
    if (track_allocations.load(std::memory_order_relaxed)) {
        allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* memory = std::malloc(size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {

using AudioBlock = CommRaT::Messages::AudioBlock;

AudioBlock make_sine(double frequency_hz, uint64_t start_frame) {
    constexpr double sample_rate_hz = 48000.0;
    constexpr double two_pi = 6.28318530717958647692;
    AudioBlock block{};
    block.sample_rate_hz = sample_rate_hz;
    block.frame_count = 512;
    block.channel_count = 1;
    for (uint32_t frame = 0; frame < block.frame_count; ++frame) {
        const double phase = two_pi * frequency_hz
            * static_cast<double>(start_frame + frame) / sample_rate_hz;
        block.channels[0].push_back(static_cast<AudioBlock::Sample>(
            0.5 * std::sin(phase)));
    }
    return block;
}

uint32_t rising_zero_crossings(const AudioBlock& block) {
    uint32_t crossings = 0;
    for (uint32_t frame = 1; frame < block.frame_count; ++frame) {
        if (block.channels[0][frame - 1] <= 0.0
            && block.channels[0][frame] > 0.0) {
            ++crossings;
        }
    }
    return crossings;
}

} // namespace

int main() {
    musicrat::dsp::PitchLockProcessor processor{48000};
    assert(processor.latency_frames() > 0);
    assert(processor.set_rate(0.5));
    assert(processor.set_rate(2.0));
    assert(!processor.set_rate(0.49));
    assert(!processor.set_rate(2.01));
    assert(processor.reset(2.0));

    AudioBlock output{};
    uint64_t source_frame = 0;
    for (uint32_t block = 0; block < 100; ++block) {
        const auto input = make_sine(880.0, source_frame);
        assert(processor.process(input, 2.0, output));
        source_frame += input.frame_count;
    }

    double sum_squares = 0.0;
    for (uint32_t frame = 0; frame < output.frame_count; ++frame) {
        const double sample = output.channels[0][frame];
        assert(std::isfinite(sample));
        sum_squares += sample * sample;
    }
    const double rms = std::sqrt(sum_squares / output.frame_count);
    assert(rms > 0.1);
    const uint32_t crossings = rising_zero_crossings(output);
    assert(crossings >= 4);
    assert(crossings <= 6);

    const auto steady_input = make_sine(880.0, source_frame);
    std::array<double, 512> ramp_rates{};
    for (uint32_t frame = 0; frame < ramp_rates.size(); ++frame) {
        ramp_rates[frame] = 1.0 + static_cast<double>(frame + 1)
            / static_cast<double>(ramp_rates.size());
    }
    allocation_count.store(0, std::memory_order_relaxed);
    track_allocations.store(true, std::memory_order_relaxed);
    for (uint32_t block = 0; block < 20; ++block) {
        assert(processor.process(steady_input, ramp_rates, output));
    }
    track_allocations.store(false, std::memory_order_relaxed);
    assert(allocation_count.load(std::memory_order_relaxed) == 0);
    assert(processor.rate() == 2.0);
}