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
    original.track_instance_id = 81;
    original.load_request_id = 82;
    original.load_state = CommRaT::Messages::MEDIA_LOAD_FAILED;
    original.load_decode_error = CommRaT::Messages::PLAYBACK_DECODE_ERROR_FILE_OPEN;
    original.beat_grid_track_instance_id = 81;
    original.track_bpm = 128.0;
    original.current_bpm = 160.0;
    original.beat_grid.segments.push_back({.start_frame = 24000, .tempo_bpm = 128});
    original.beat_grid.segments.push_back({.start_frame = 69000, .start_beat = 2, .tempo_bpm = 140});
    original.beat_grid.beats_per_bar = 3;
    original.beat_grid.beat_unit = 8;
    original.beat_grid.confidence_per_mille = 900;
    original.beat_grid.flags = 7;
    original.marker_track_instance_id = 81;
    original.markers = {.cue_frame = 0, .loop_start_frame = 48000, .loop_end_frame = 96000,
        .cue_set = true, .loop_start_set = true, .loop_end_set = true, .loop_enabled = true};
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
    assert(restored->track_instance_id == original.track_instance_id);
    assert(restored->load_request_id == original.load_request_id);
    assert(restored->load_state == original.load_state);
    assert(restored->load_decode_error == original.load_decode_error);
    assert(restored->beat_grid_track_instance_id == original.beat_grid_track_instance_id);
    assert(restored->track_bpm == original.track_bpm);
    assert(restored->current_bpm == original.current_bpm);
    assert(restored->beat_grid == original.beat_grid);
    assert(restored->marker_track_instance_id == original.marker_track_instance_id);
    assert(restored->markers == original.markers);
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

    original.beat_grid.segments.clear();
    for (uint32_t index = 0; index < CommRaT::Messages::MAX_BEAT_GRID_SEGMENTS; ++index) {
        original.beat_grid.segments.push_back({
            .start_frame = index * uint64_t{90000}, .start_beat = index * 4.0, .tempo_bpm = 128});
    }
    const auto full = sertial::Message<PlaybackStatusBlock>::serialize(original);
    const auto full_restored = sertial::Message<PlaybackStatusBlock>::deserialize(full.view());
    assert(full_restored && full_restored->beat_grid == original.beat_grid);
}