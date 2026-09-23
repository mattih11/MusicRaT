#pragma once

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

inline constexpr std::size_t MAX_BEAT_GRID_SEGMENTS = 32;

struct BeatGridSegment {
    uint64_t start_frame{0};
    double start_beat{0.0};
    double tempo_bpm{120.0};
};

struct BeatGrid {
    sertial::fixed_vector<BeatGridSegment, MAX_BEAT_GRID_SEGMENTS> segments{};
    double sample_rate_hz{48000.0};
    uint16_t beats_per_bar{4};
    uint16_t beat_unit{4};
    uint16_t confidence_per_mille{0};
    uint16_t flags{0};
};

using TransportState = uint8_t;

inline constexpr TransportState TRANSPORT_STOPPED = 0;
inline constexpr TransportState TRANSPORT_PLAYING = 1;
inline constexpr TransportState TRANSPORT_PAUSED = 2;
inline constexpr TransportState TRANSPORT_RECORDING = 3;

using TransportSyncMode = uint8_t;

inline constexpr TransportSyncMode TRANSPORT_SYNC_OFF = 0;
inline constexpr TransportSyncMode TRANSPORT_SYNC_TEMPO = 1;
inline constexpr TransportSyncMode TRANSPORT_SYNC_BEAT = 2;
inline constexpr TransportSyncMode TRANSPORT_SYNC_BAR = 3;

enum TransportFlag : uint16_t {
    TRANSPORT_DISCONTINUITY = 1U << 0U,
    TRANSPORT_SEEK = 1U << 1U,
    TRANSPORT_LOOPING = 1U << 2U,
    TRANSPORT_CLOCK_SOURCE_CHANGED = 1U << 3U,
};

struct TransportBlock {
    double beat_position{0.0};
    double tempo_bpm{120.0};
    uint64_t transport_frame{0};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
    uint32_t sample_rate_hz{48000};
    uint16_t beats_per_bar{4};
    uint16_t beat_unit{4};
    TransportState state{TRANSPORT_STOPPED};
    uint16_t flags{0};
};

} // namespace CommRaT::Messages

namespace CommRaT::Parameters {

inline constexpr uint32_t TRANSPORT_TEMPO_PARAMETER_ID = 1;
inline constexpr uint32_t TRANSPORT_INITIAL_BEAT_PARAMETER_ID = 2;
inline constexpr uint32_t TRANSPORT_SAMPLE_RATE_PARAMETER_ID = 3;
inline constexpr uint32_t TRANSPORT_BEATS_PER_BAR_PARAMETER_ID = 4;
inline constexpr uint32_t TRANSPORT_BEAT_UNIT_PARAMETER_ID = 5;
inline constexpr uint32_t TRANSPORT_STATE_PARAMETER_ID = 6;

struct TransportSource {
    double tempo_bpm{120.0};
    double initial_beat_position{0.0};
    uint32_t sample_rate_hz{48000};
    uint16_t beats_per_bar{4};
    uint16_t beat_unit{4};
    CommRaT::Messages::TransportState state{
        CommRaT::Messages::TRANSPORT_PLAYING};
};

} // namespace CommRaT::Parameters