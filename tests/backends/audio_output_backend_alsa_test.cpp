#include <musicrat/backends/audio/audio_output_backend.hpp>
#include <musicrat/config.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <string>

int main() {
    using musicrat::backends::audio::AudioOutputConfig;
    using musicrat::backends::audio::make_audio_output_backend;

    auto backend = make_audio_output_backend();
    assert(backend != nullptr);
    assert(std::string{backend->name()} == "alsa");
    assert(!backend->open(AudioOutputConfig{}));
    assert(!backend->open(AudioOutputConfig{
        .sample_rate_hz = 48000,
        .channel_count = static_cast<uint16_t>(
            musicrat::config::max_audio_channels + 1),
        .period_frames = 480,
    }));
    assert(!backend->open(AudioOutputConfig{
        .sample_rate_hz = 48000,
        .channel_count = 2,
        .period_frames = musicrat::config::max_audio_frames + 1,
    }));

    const auto frames = static_cast<uint32_t>(
        std::min<std::size_t>(480, musicrat::config::max_audio_frames));
    const AudioOutputConfig null_config{
        .device_name = "null",
        .sample_rate_hz = 48000,
        .channel_count = 1,
        .period_frames = frames,
    };
    CommRaT::Messages::AudioBlock block{};
    block.sample_rate_hz = 48000;
    block.channel_count = 1;
    block.frame_count = frames;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        block.channels[0].push_back(0.25F);
    }
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        assert(backend->open(null_config));
        assert(backend->underrun_count() == 0);
        assert(backend->stream_failure_count() == 0);
        assert(backend->queue_capacity() > 0);
        assert(backend->queued_frames() <= backend->queue_capacity());
        assert(backend->queue_high_water_frames() == 0);
        assert(backend->rate_correction_ppm() == 0);
        assert(backend->maximum_rate_correction_ppm() == 0);
        assert(backend->write(block));
        assert(backend->queue_high_water_frames() >= frames);
        assert(backend->queue_high_water_frames()
            <= backend->queue_capacity());
        backend->close();
        assert(!backend->write(block));
    }
    std::array<musicrat::backends::audio::AudioOutputDiagnosticEvent, 8> events{};
    musicrat::backends::audio::AudioOutputDiagnostics capture{.events = events};
    auto diagnostic_config = null_config;
    diagnostic_config.diagnostics = &capture;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        capture.count = events.size();
        capture.dropped_events = 42;
        assert(backend->open(diagnostic_config));
        backend->close();
        assert(capture.supported);
        assert(capture.count <= events.size());
        assert(capture.dropped_events == 0);
        assert(capture.device_period_frames > 0);
        assert(capture.device_buffer_frames >= capture.device_period_frames);
        assert(capture.device_sample_rate_hz == null_config.sample_rate_hz);
        assert(capture.slow_operation_threshold_ns > 0);
        assert(capture.start_timestamp_ns > 0);
    }
}