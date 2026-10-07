#include <musicrat/backends/audio/audio_output_backend.hpp>
#include <musicrat/config.hpp>

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
}