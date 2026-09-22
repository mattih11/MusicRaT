#include <musicrat/backends/audio/wav_writer.hpp>
#include <musicrat/modules/audio_file_player.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <thread>

namespace {

class TestAudioFilePlayer : public CommRaT::AudioFilePlayer {
public:
    using CommRaT::AudioFilePlayer::AudioFilePlayer;
    using CommRaT::AudioFilePlayer::on_enable;
    using CommRaT::AudioFilePlayer::on_start;
    using CommRaT::AudioFilePlayer::on_stop;
    using CommRaT::AudioFilePlayer::process;
};

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.0001;
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path()
        / "musicrat_audio_file_player_test.wav";

    CommRaT::Messages::AudioBlock source{};
    source.sample_rate_hz = 48000.0;
    source.frame_count = 960;
    source.channel_count = 1;
    for (uint32_t frame = 0; frame < source.frame_count; ++frame) {
        source.channels[0].push_back(static_cast<double>(frame) / 1000.0);
    }
    {
        musicrat::backends::audio::WavWriter writer{};
        assert(writer.open(path.c_str(), 48000, 1));
        assert(writer.write(source));
    }

    commrat::ModuleConfig config{};
    config.name = "AudioFilePlayerTest";
    config.outputs = commrat::MultiOutputConfig{.addresses = {
        {.system_id = 41, .instance_id = 1},
        {.system_id = 43, .instance_id = 1},
    }};
    config.inputs = commrat::MultiInputConfig{
        .sources = {{
            .system_id = 42,
            .instance_id = 1,
            .lifecycle_address = 0,
            .is_primary = false,
        }},
        .history_buffer_size = 8,
        .sync_tolerance = std::chrono::milliseconds{20},
    };
    config.period = std::chrono::milliseconds{10};
    config.params = rfl::json::read<rfl::Generic>(
        "{\"path\":\"" + path.string()
        + "\",\"output_sample_rate_hz\":48000.0,"
          "\"initial_rate\":1.0,\"autoplay\":true}").value();

    auto player = std::make_unique<TestAudioFilePlayer>(config);
    auto output = std::make_unique<CommRaT::Messages::AudioBlock>();
    CommRaT::Messages::PlaybackStatusBlock status{};
    const commrat::Synced<CommRaT::Messages::DeckControlEventBlock> controls{};
    const commrat::Synced<CommRaT::Messages::TransportBlock> transport{};

    player->on_start();
    assert(player->on_enable() == commrat::LifecycleResult::Success);

    bool rendered_audio = false;
    for (uint32_t attempt = 0; attempt < 10000; ++attempt) {
        player->process(controls, transport, *output, status);
        if (output->channel_count == 1
            && output->channels[0].size() == 480
            && near(output->channels[0][1], 0.001)) {
            rendered_audio = true;
            break;
        }
        std::this_thread::yield();
    }

    player->on_stop();
    assert(rendered_audio);
    assert(status.media_position_frames > 0.0);
    assert(status.playback_rate == 1.0);
    assert(status.minimum_playback_rate == 0.25);
    assert(status.maximum_playback_rate == 4.0);
    assert(status.generation > 0);
    assert(status.timestamp_ns == output->timestamp_ns);
    assert(status.sequence_number == output->sequence_number);
    assert(status.channel_count == output->channel_count);
    assert(status.source_channel_count == 1);
    assert(status.source_sample_rate_hz == 48000);
    assert(status.duration_frames == 960);
    assert(status.resident_frames == 960);
    assert(status.buffered_start_frame == 0);
    assert(status.buffered_end_frame == 960);
    assert(status.codec == CommRaT::Messages::PLAYBACK_CODEC_PCM_WAV);
    assert(status.worker_state
        == CommRaT::Messages::PLAYBACK_WORKER_END_OF_STREAM);
    assert(status.decode_error == CommRaT::Messages::PLAYBACK_DECODE_ERROR_NONE);
    assert(status.algorithmic_latency_frames == 0);
    assert(status.pitch_mode == CommRaT::Messages::PLAYBACK_PITCH_MODE_VARISPEED);
    assert((status.flags & CommRaT::Messages::PLAYBACK_STATUS_READY) != 0);
    assert((status.flags & CommRaT::Messages::PLAYBACK_STATUS_PLAYING) != 0);
    assert((status.flags & CommRaT::Messages::PLAYBACK_STATUS_DISCONTINUITY) != 0);

#if MUSICRAT_HAS_RUBBERBAND
    config.name = "PitchLockedAudioFilePlayerTest";
    config.params = rfl::json::read<rfl::Generic>(
        "{\"path\":\"" + path.string()
        + "\",\"output_sample_rate_hz\":48000.0,"
          "\"initial_rate\":1.0,\"autoplay\":true,\"pitch_lock\":true}").value();
    player = std::make_unique<TestAudioFilePlayer>(config);
    status = {};
    player->on_start();
    assert(player->on_enable() == commrat::LifecycleResult::Success);
    bool published_pitch_lock = false;
    for (uint32_t attempt = 0; attempt < 10000; ++attempt) {
        player->process(controls, transport, *output, status);
        if (status.generation > 0
            && status.pitch_mode == CommRaT::Messages::PLAYBACK_PITCH_MODE_LOCKED) {
            published_pitch_lock = true;
            break;
        }
        std::this_thread::yield();
    }
    player->on_stop();
    assert(published_pitch_lock);
    assert(status.algorithmic_latency_frames > 0);
    assert(status.minimum_playback_rate == 0.5);
    assert(status.maximum_playback_rate == 2.0);
#endif
    std::filesystem::remove(path);
}