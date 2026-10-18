#include <musicrat/backends/audio/audio_output_backend.hpp>
#include <musicrat/config.hpp>

#include <algorithm>
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
        assert(backend->write(block));
        backend->close();
        assert(!backend->write(block));
    }
}