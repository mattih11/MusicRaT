#include <musicrat/protocol/playback_status.hpp>

#include <sertial/message.hpp>

#include <cassert>

int main() {
    using CommRaT::Messages::PlaybackStatusBlock;

    PlaybackStatusBlock original{};
    original.media_position_frames = 1440.5;
    original.playback_rate = 1.25;
    original.minimum_playback_rate = 0.5;
    original.maximum_playback_rate = 2.0;
    original.beat_position = 7.25;
    original.transport_beat_position = 7.5;
    original.sync_phase_error_beats = 0.25;
    original.generation = 3;
    original.timestamp_ns = 1234;
    original.sequence_number = 9;
    original.duration_frames = 96000;
    original.resident_frames = 8192;
    original.buffered_start_frame = 4096;
    original.buffered_end_frame = 12288;
    original.quantized_actions_dropped = 3;
    original.deck_control_blocks_received = 11;
    original.transport_blocks_received = 12;
    original.source_sample_rate_hz = 48000;
    original.algorithmic_latency_frames = 1024;
    original.quantized_actions_pending = 2;
    original.channel_count = 2;
    original.source_channel_count = 2;
    original.codec = CommRaT::Messages::PLAYBACK_CODEC_PCM_WAV;
    original.worker_state = CommRaT::Messages::PLAYBACK_WORKER_READY;
    original.decode_error = CommRaT::Messages::PLAYBACK_DECODE_ERROR_CORRUPT_DATA;
    original.pitch_mode = CommRaT::Messages::PLAYBACK_PITCH_MODE_LOCKED;
    original.transport_sync_mode = CommRaT::Messages::TRANSPORT_SYNC_BEAT;
    original.flags = CommRaT::Messages::PLAYBACK_STATUS_READY
        | CommRaT::Messages::PLAYBACK_STATUS_PLAYING
        | CommRaT::Messages::PLAYBACK_STATUS_LOOPING;

    const auto serialized = sertial::Message<PlaybackStatusBlock>::serialize(original);
    const auto restored = sertial::Message<PlaybackStatusBlock>::deserialize(
        serialized.view());

    assert(restored);
    assert(restored->media_position_frames == original.media_position_frames);
    assert(restored->playback_rate == original.playback_rate);
    assert(restored->minimum_playback_rate == original.minimum_playback_rate);
    assert(restored->maximum_playback_rate == original.maximum_playback_rate);
    assert(restored->beat_position == original.beat_position);
    assert(restored->transport_beat_position == original.transport_beat_position);
    assert(restored->sync_phase_error_beats == original.sync_phase_error_beats);
    assert(restored->generation == original.generation);
    assert(restored->timestamp_ns == original.timestamp_ns);
    assert(restored->sequence_number == original.sequence_number);
    assert(restored->duration_frames == original.duration_frames);
    assert(restored->resident_frames == original.resident_frames);
    assert(restored->buffered_start_frame == original.buffered_start_frame);
    assert(restored->buffered_end_frame == original.buffered_end_frame);
    assert(restored->quantized_actions_dropped
        == original.quantized_actions_dropped);
    assert(restored->deck_control_blocks_received
        == original.deck_control_blocks_received);
    assert(restored->transport_blocks_received
        == original.transport_blocks_received);
    assert(restored->source_sample_rate_hz == original.source_sample_rate_hz);
    assert(restored->algorithmic_latency_frames
        == original.algorithmic_latency_frames);
    assert(restored->quantized_actions_pending
        == original.quantized_actions_pending);
    assert(restored->channel_count == original.channel_count);
    assert(restored->source_channel_count == original.source_channel_count);
    assert(restored->codec == original.codec);
    assert(restored->worker_state == original.worker_state);
    assert(restored->decode_error == original.decode_error);
    assert(restored->pitch_mode == original.pitch_mode);
    assert(restored->transport_sync_mode == original.transport_sync_mode);
    assert(restored->flags == original.flags);
}