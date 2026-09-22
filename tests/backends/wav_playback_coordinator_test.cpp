#include <musicrat/backends/audio/wav_writer.hpp>
#include <musicrat/backends/media/wav_playback_coordinator.hpp>

#include <cassert>
#include <cmath>
#include <filesystem>

namespace {

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.0001;
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path()
        / "musicrat_wav_playback_coordinator_test.wav";

    CommRaT::Messages::AudioBlock source{};
    source.sample_rate_hz = 48000.0;
    source.frame_count = 6;
    source.channel_count = 1;
    for (uint32_t frame = 0; frame < source.frame_count; ++frame) {
        source.channels[0].push_back(static_cast<double>(frame) / 10.0);
    }
    {
        musicrat::backends::audio::WavWriter writer{};
        assert(writer.open(path.c_str(), 48000, 1));
        assert(writer.write(source));
    }

    musicrat::backends::media::WavPlaybackCoordinator<3> player{2, 3};
    decltype(player)::PlaybackDiagnostics diagnostics{};
    assert(player.try_read_diagnostics(diagnostics));
    assert(diagnostics.worker_state
        == musicrat::backends::media::DecodeAheadState::Closed);
    assert(diagnostics.codec == musicrat::backends::media::MediaCodec::Unknown);
    assert(diagnostics.duration_frames == 0);
    assert(diagnostics.resident_frames == 0);
    assert(diagnostics.buffered_start_frame == 0);
    assert(diagnostics.buffered_end_frame == 0);
    assert(diagnostics.decode_error
        == musicrat::backends::media::DecodeError::None);

    assert(player.worker_open(path.c_str()));
    assert(player.try_read_diagnostics(diagnostics));
    assert(diagnostics.worker_state
        == musicrat::backends::media::DecodeAheadState::Ready);
    assert(diagnostics.codec == musicrat::backends::media::MediaCodec::PcmWav);
    assert(diagnostics.source_sample_rate_hz == 48000);
    assert(diagnostics.source_channel_count == 1);
    assert(diagnostics.duration_frames == 6);
    assert(diagnostics.resident_frames == 0);
    assert(diagnostics.decode_error
        == musicrat::backends::media::DecodeError::None);

    CommRaT::Messages::AudioBlock output{};
    auto result = player.render(48000.0, 2, 1000, 1, output);
    assert(result.silent_frames == 2);
    assert(near(player.media_position(), 0.0));
    assert(output.channel_count == 1);
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_UNDERRUN);

    assert(player.worker_refill().chunks_published == 3);
    assert(player.try_read_diagnostics(diagnostics));
    assert(diagnostics.worker_state
        == musicrat::backends::media::DecodeAheadState::EndOfStream);
    assert(diagnostics.resident_frames == 6);
    assert(diagnostics.buffered_start_frame == 0);
    assert(diagnostics.buffered_end_frame == 6);
    result = player.render(48000.0, 2, 2000, 2, output);
    assert(result.source_frames_rendered == 2);
    assert(near(output.channels[0][0], 0.0));
    assert(near(output.channels[0][1], 0.1));
    assert(near(player.media_position(), 2.0));
    assert(player.try_read_diagnostics(diagnostics));
    assert(diagnostics.resident_frames == 4);
    assert(diagnostics.buffered_start_frame == 2);
    assert(diagnostics.buffered_end_frame == 6);

    assert(!player.worker_seek(99));
    assert(player.try_read_diagnostics(diagnostics));
    assert(diagnostics.decode_error
        == musicrat::backends::media::DecodeError::InvalidArgument);
    result = player.render(48000.0, 1, 2500, 3, output);
    assert(result.source_frames_rendered == 1);
    assert(near(output.channels[0][0], 0.2));
    assert(near(player.media_position(), 3.0));

    assert(player.worker_seek(4));
    result = player.render(48000.0, 2, 3000, 4, output);
    assert(result.silent_frames == 2);
    assert(near(player.media_position(), 4.0));
    assert(output.flags & CommRaT::Messages::AUDIO_BLOCK_UNDERRUN);
    assert(player.try_read_diagnostics(diagnostics));
    assert(diagnostics.worker_state
        == musicrat::backends::media::DecodeAheadState::Ready);
    assert(diagnostics.duration_frames == 6);
    assert(diagnostics.resident_frames == 0);
    assert(diagnostics.buffered_start_frame == 0);
    assert(diagnostics.buffered_end_frame == 0);
    assert(diagnostics.decode_error
        == musicrat::backends::media::DecodeError::None);

    assert(player.worker_refill().chunks_published == 1);
    assert(player.try_read_diagnostics(diagnostics));
    assert(diagnostics.buffered_start_frame == 4);
    assert(diagnostics.buffered_end_frame == 6);
    result = player.render(48000.0, 2, 4000, 5, output);
    assert(result.source_frames_rendered == 2);
    assert(result.silent_frames == 0);
    assert(near(output.channels[0][0], 0.4));
    assert(near(output.channels[0][1], 0.5));
    assert(near(player.media_position(), 6.0));

    player.worker_close();
    assert(player.try_read_diagnostics(diagnostics));
    assert(diagnostics.worker_state
        == musicrat::backends::media::DecodeAheadState::Closed);
    assert(diagnostics.codec == musicrat::backends::media::MediaCodec::Unknown);
    assert(diagnostics.duration_frames == 0);
    assert(diagnostics.resident_frames == 0);
    assert(diagnostics.decode_error
        == musicrat::backends::media::DecodeError::None);

    assert(!player.worker_open("/musicrat/missing/audio.wav"));
    assert(player.try_read_diagnostics(diagnostics));
    assert(diagnostics.worker_state
        == musicrat::backends::media::DecodeAheadState::Error);
    assert(diagnostics.codec == musicrat::backends::media::MediaCodec::Unknown);
    assert(diagnostics.duration_frames == 0);
    assert(diagnostics.resident_frames == 0);
    assert(diagnostics.decode_error
        == musicrat::backends::media::DecodeError::FileOpen);
    std::filesystem::remove(path);
}