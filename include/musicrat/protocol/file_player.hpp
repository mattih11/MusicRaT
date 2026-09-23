#pragma once

#include <musicrat/config.hpp>
#include <musicrat/protocol/transport.hpp>

#include <sertial/containers/fixed_string.hpp>

#include <cstdint>

namespace CommRaT::Messages {

struct LoadMedia {
    sertial::fixed_string<512> path{};

    struct Reply {
        bool accepted{false};
    };
};

struct SeekMedia {
    uint64_t media_frame{0};

    struct Reply {
        bool accepted{false};
    };
};

struct UnloadMedia {
    struct Reply {
        bool accepted{false};
    };
};

} // namespace CommRaT::Messages

namespace CommRaT::Parameters {

inline constexpr uint32_t FILE_PLAYER_PATH_PARAMETER_ID = 1;
inline constexpr uint32_t FILE_PLAYER_OUTPUT_SAMPLE_RATE_PARAMETER_ID = 2;
inline constexpr uint32_t FILE_PLAYER_INITIAL_RATE_PARAMETER_ID = 3;
inline constexpr uint32_t FILE_PLAYER_AUTOPLAY_PARAMETER_ID = 4;
inline constexpr uint32_t FILE_PLAYER_PITCH_LOCK_PARAMETER_ID = 5;
inline constexpr uint32_t FILE_PLAYER_TRANSPORT_SYNC_MODE_PARAMETER_ID = 6;
inline constexpr uint32_t FILE_PLAYER_SYNC_RATE_RAMP_PARAMETER_ID = 7;

struct FilePlayer {
    sertial::fixed_string<512> path{};
    double output_sample_rate_hz{musicrat::config::default_sample_rate_hz};
    double initial_rate{1.0};
    bool autoplay{true};
    bool pitch_lock{false};
    CommRaT::Messages::BeatGrid beat_grid{};
    CommRaT::Messages::TransportSyncMode transport_sync_mode{
        CommRaT::Messages::TRANSPORT_SYNC_OFF};
    uint32_t sync_rate_ramp_frames{480};
};

} // namespace CommRaT::Parameters