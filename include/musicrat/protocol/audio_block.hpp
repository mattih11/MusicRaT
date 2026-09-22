#pragma once

#include <musicrat/config.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

enum AudioBlockFlag : uint16_t {
    AUDIO_BLOCK_SILENCE = 1U << 0U,
    AUDIO_BLOCK_END_OF_STREAM = 1U << 1U,
    AUDIO_BLOCK_UNDERRUN = 1U << 2U,
    AUDIO_BLOCK_DISCONTINUITY = 1U << 3U,
    AUDIO_BLOCK_INVALID = 1U << 4U,
};

struct AudioBlock {
    using Sample = musicrat::config::sample_type;
    using Channel = sertial::fixed_vector<Sample, musicrat::config::max_audio_frames>;

    static constexpr std::size_t MAX_CHANNELS = musicrat::config::max_audio_channels;
    static constexpr std::size_t MAX_FRAMES = musicrat::config::max_audio_frames;

    std::array<Channel, MAX_CHANNELS> channels{};
    double sample_rate_hz{0.0};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
    uint32_t frame_count{0};
    uint16_t channel_count{0};
    uint16_t flags{0};
};

} // namespace CommRaT::Messages