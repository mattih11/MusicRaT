#pragma once

#include <musicrat/config.hpp>

#include <sertial/containers/fixed_string.hpp>

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t AUDIO_DEVICE_SINK_DEVICE_PARAMETER_ID = 1;
inline constexpr uint32_t AUDIO_DEVICE_SINK_SAMPLE_RATE_PARAMETER_ID = 2;
inline constexpr uint32_t AUDIO_DEVICE_SINK_CHANNEL_COUNT_PARAMETER_ID = 3;
inline constexpr uint32_t AUDIO_DEVICE_SINK_PERIOD_FRAMES_PARAMETER_ID = 4;

struct AudioDeviceSink {
    sertial::fixed_string<256> device_name{};
    uint32_t sample_rate_hz{
        static_cast<uint32_t>(musicrat::config::default_sample_rate_hz)};
    uint16_t channel_count{2};
    uint32_t period_frames{static_cast<uint32_t>(
        musicrat::config::default_sample_rate_hz
        * musicrat::config::default_block_period_ms / 1000.0)};
};

} // namespace CommRaT::Parameters