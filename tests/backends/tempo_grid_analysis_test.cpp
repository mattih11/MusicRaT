#include <musicrat/backends/audio/wav_writer.hpp>
#include <musicrat/backends/media/tempo_grid_analysis.hpp>

#include "../support/directory_fixture.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <thread>

namespace {

void write_click_track(const std::filesystem::path& path, double bpm) {
    constexpr uint32_t sample_rate = 48000;
    constexpr uint64_t duration_frames = sample_rate * 90;
    const auto beat_stride = static_cast<uint64_t>(std::llround(sample_rate * 60.0 / bpm));
    musicrat::backends::audio::WavWriter writer{};
    assert(writer.open(path.c_str(), sample_rate, 1));
    CommRaT::Messages::AudioBlock block{};
    uint64_t frame = 0;
    while (frame < duration_frames) {
        block = {};
        block.sample_rate_hz = sample_rate;
        block.channel_count = 1;
        block.frame_count = static_cast<uint32_t>(std::min<uint64_t>(
            CommRaT::Messages::AudioBlock::MAX_FRAMES, duration_frames - frame));
        for (uint32_t index = 0; index < block.frame_count; ++index) {
            const auto position = frame + index;
            const auto in_click = (position % beat_stride) < 320;
            const auto sample = in_click ? 0.9 : 0.0;
            block.channels[0].push_back(sample);
        }
        assert(writer.write(block));
        frame += block.frame_count;
    }
}

bool near(double actual, double expected, double tolerance) {
    return std::abs(actual - expected) <= tolerance;
}

} // namespace

int main() {
    musicrat::testing::TestDirectory fixture;
    const auto click = fixture.path / "click.wav";
    write_click_track(click, 128.0);

    musicrat::backends::media::TempoGridAnalysisWorker worker{};
    assert(worker.submit(9, click.c_str()));
    musicrat::backends::media::TempoGridAnalysisResult result{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.poll(result) && result.request_id == 9) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    assert(result.request_id == 9);
    assert(result.valid && result.sample_rate_hz == 48000);
    assert(near(result.tempo_bpm, 128.0, 2.0));
    assert(result.confidence_per_mille >= 120);

    assert(worker.submit(10, (fixture.path / "missing.wav").c_str()));
    result = {};
    const auto second_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < second_deadline) {
        if (worker.poll(result) && result.request_id == 10) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    assert(result.request_id == 10);
    assert(!result.valid);
}
