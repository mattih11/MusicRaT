#pragma once

#include <musicrat/protocol/transport.hpp>

#include <cstdint>

namespace CommRaT::Messages {

using PlaybackCodec = uint8_t;

inline constexpr PlaybackCodec PLAYBACK_CODEC_UNKNOWN = 0;
inline constexpr PlaybackCodec PLAYBACK_CODEC_PCM_WAV = 1;
inline constexpr PlaybackCodec PLAYBACK_CODEC_FLAC = 2;
inline constexpr PlaybackCodec PLAYBACK_CODEC_MP3 = 3;
inline constexpr PlaybackCodec PLAYBACK_CODEC_AAC = 4;
inline constexpr PlaybackCodec PLAYBACK_CODEC_OPUS = 5;

using PlaybackWorkerState = uint8_t;

inline constexpr PlaybackWorkerState PLAYBACK_WORKER_CLOSED = 0;
inline constexpr PlaybackWorkerState PLAYBACK_WORKER_PREPARING = 1;
inline constexpr PlaybackWorkerState PLAYBACK_WORKER_READY = 2;
inline constexpr PlaybackWorkerState PLAYBACK_WORKER_END_OF_STREAM = 3;
inline constexpr PlaybackWorkerState PLAYBACK_WORKER_ERROR = 4;

using PlaybackDecodeError = uint8_t;

inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_NONE = 0;
inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_INVALID_ARGUMENT = 1;
inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_FILE_OPEN = 2;
inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_FILE_IO = 3;
inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_UNSUPPORTED_FORMAT = 4;
inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_INVALID_METADATA = 5;
inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_RESOURCE_EXHAUSTED = 6;
inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_SEEK_FAILED = 7;
inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_CORRUPT_DATA = 8;
inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_DECODER_FAILURE = 9;
inline constexpr PlaybackDecodeError PLAYBACK_DECODE_ERROR_POOL_FAILURE = 10;

using PlaybackPitchMode = uint8_t;

inline constexpr PlaybackPitchMode PLAYBACK_PITCH_MODE_VARISPEED = 0;
inline constexpr PlaybackPitchMode PLAYBACK_PITCH_MODE_LOCKED = 1;

enum PlaybackStatusFlag : uint16_t {
    PLAYBACK_STATUS_READY = 1U << 0U,
    PLAYBACK_STATUS_PLAYING = 1U << 1U,
    PLAYBACK_STATUS_UNDERRUN = 1U << 2U,
    PLAYBACK_STATUS_END_OF_STREAM = 1U << 3U,
    PLAYBACK_STATUS_DISCONTINUITY = 1U << 4U,
    PLAYBACK_STATUS_INVALID = 1U << 5U,
    PLAYBACK_STATUS_LOOPING = 1U << 6U,
    PLAYBACK_STATUS_TRANSPORT_FOLLOWING = 1U << 7U,
    PLAYBACK_STATUS_SYNC_SEEK_PENDING = 1U << 8U,
    PLAYBACK_STATUS_QUANTIZED_ACTION_PENDING = 1U << 9U,
};

struct PlaybackStatusBlock {
    double media_position_frames{0.0};
    double playback_rate{1.0};
    double minimum_playback_rate{0.25};
    double maximum_playback_rate{4.0};
    double beat_position{0.0};
    double transport_beat_position{0.0};
    double sync_phase_error_beats{0.0};
    uint64_t generation{0};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
    uint64_t duration_frames{0};
    uint64_t resident_frames{0};
    uint64_t buffered_start_frame{0};
    uint64_t buffered_end_frame{0};
    uint64_t quantized_actions_dropped{0};
    uint64_t deck_control_blocks_received{0};
    uint64_t transport_blocks_received{0};
    uint32_t source_sample_rate_hz{0};
    uint32_t algorithmic_latency_frames{0};
    uint32_t quantized_actions_pending{0};
    uint16_t channel_count{0};
    uint16_t source_channel_count{0};
    PlaybackCodec codec{PLAYBACK_CODEC_UNKNOWN};
    PlaybackWorkerState worker_state{PLAYBACK_WORKER_CLOSED};
    PlaybackDecodeError decode_error{PLAYBACK_DECODE_ERROR_NONE};
    PlaybackPitchMode pitch_mode{PLAYBACK_PITCH_MODE_VARISPEED};
    TransportSyncMode transport_sync_mode{TRANSPORT_SYNC_OFF};
    uint16_t flags{0};
};

} // namespace CommRaT::Messages