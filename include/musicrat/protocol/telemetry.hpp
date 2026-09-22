#pragma once

#include <musicrat/config.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

enum LevelMeterFlag : uint16_t {
    LEVEL_METER_INVALID = 1U << 0U,
};

struct LevelMeterBlock {
    using Sample = musicrat::config::sample_type;

    static constexpr std::size_t MAX_CHANNELS = musicrat::config::max_audio_channels;

    std::array<Sample, MAX_CHANNELS> peak{};
    std::array<Sample, MAX_CHANNELS> rms{};
    std::array<uint8_t, MAX_CHANNELS> clipped{};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
    uint16_t channel_count{0};
    uint16_t flags{0};
};

} // namespace CommRaT::Messages

namespace CommRaT::Parameters {

struct LevelMeter {
    musicrat::config::sample_type clipping_threshold{1.0};
};

} // namespace CommRaT::Parameters