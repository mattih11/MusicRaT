#pragma once

#include <musicrat/config.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t CHANNEL_MAPPER_MODE_PARAMETER_ID = 1;
inline constexpr uint32_t CHANNEL_MAPPER_INPUT_CHANNELS_PARAMETER_ID = 2;
inline constexpr uint32_t CHANNEL_MAPPER_OUTPUT_CHANNELS_PARAMETER_ID = 3;

using ChannelMapMode = uint8_t;

inline constexpr ChannelMapMode CHANNEL_MAP_IDENTITY = 0;
inline constexpr ChannelMapMode CHANNEL_MAP_MONO_TO_STEREO = 1;
inline constexpr ChannelMapMode CHANNEL_MAP_STEREO_TO_MONO = 2;
inline constexpr ChannelMapMode CHANNEL_MAP_SWAP_STEREO = 3;
inline constexpr ChannelMapMode CHANNEL_MAP_COPY_LEFT = 4;
inline constexpr ChannelMapMode CHANNEL_MAP_COPY_RIGHT = 5;
inline constexpr ChannelMapMode CHANNEL_MAP_MATRIX = 6;

struct ChannelMapper {
    static constexpr std::size_t MAX_COEFFICIENTS =
        musicrat::config::max_audio_channels
        * musicrat::config::max_audio_channels;

    ChannelMapMode mode{CHANNEL_MAP_IDENTITY};
    uint16_t input_channel_count{2};
    uint16_t output_channel_count{2};
    sertial::fixed_vector<
        musicrat::config::sample_type,
        MAX_COEFFICIENTS> coefficients{};
};

} // namespace CommRaT::Parameters