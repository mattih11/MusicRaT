#pragma once

#include <musicrat/config.hpp>

#include <sertial/containers/fixed_string.hpp>

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t WAV_SINK_PATH_PARAMETER_ID = 1;
inline constexpr uint32_t WAV_SINK_SAMPLE_RATE_PARAMETER_ID = 2;
inline constexpr uint32_t WAV_SINK_CHANNEL_COUNT_PARAMETER_ID = 3;

struct WavSink {
    sertial::fixed_string<512> path{"musicrat-output.wav"};
    uint32_t sample_rate_hz{
        static_cast<uint32_t>(musicrat::config::default_sample_rate_hz)};
    uint16_t channel_count{1};
};

} // namespace CommRaT::Parameters