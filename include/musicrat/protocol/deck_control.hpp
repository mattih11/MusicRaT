#pragma once

#include <musicrat/config.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

using DeckControlType = uint8_t;

inline constexpr DeckControlType DECK_CONTROL_PLAY = 0;
inline constexpr DeckControlType DECK_CONTROL_PAUSE = 1;
inline constexpr DeckControlType DECK_CONTROL_SET_RATE = 2;
inline constexpr DeckControlType DECK_CONTROL_RAMP_RATE = 3;
inline constexpr DeckControlType DECK_CONTROL_SET_CUE = 4;
inline constexpr DeckControlType DECK_CONTROL_RETURN_TO_CUE = 5;
inline constexpr DeckControlType DECK_CONTROL_CLEAR_CUE = 6;
inline constexpr DeckControlType DECK_CONTROL_SET_LOOP_START = 7;
inline constexpr DeckControlType DECK_CONTROL_SET_LOOP_END = 8;
inline constexpr DeckControlType DECK_CONTROL_ENABLE_LOOP = 9;
inline constexpr DeckControlType DECK_CONTROL_DISABLE_LOOP = 10;

using DeckControlQuantization = uint8_t;

inline constexpr DeckControlQuantization DECK_QUANTIZE_IMMEDIATE = 0;
inline constexpr DeckControlQuantization DECK_QUANTIZE_BEAT = 1;
inline constexpr DeckControlQuantization DECK_QUANTIZE_BAR = 2;

struct DeckControlEvent {
    DeckControlType type{DECK_CONTROL_PLAY};
    uint32_t sample_offset{0};
    double value{0.0};
    uint32_t ramp_frames{0};
    DeckControlQuantization quantization{DECK_QUANTIZE_IMMEDIATE};
};

struct DeckControlEventBlock {
    static constexpr std::size_t MAX_EVENTS = musicrat::config::max_parameter_events;

    sertial::fixed_vector<DeckControlEvent, MAX_EVENTS> events{};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
};

} // namespace CommRaT::Messages

namespace CommRaT::Parameters {

struct DeckControlSource {
    CommRaT::Messages::DeckControlType type{
        CommRaT::Messages::DECK_CONTROL_PLAY};
    double value{1.0};
    uint32_t ramp_frames{0};
    CommRaT::Messages::DeckControlQuantization quantization{
        CommRaT::Messages::DECK_QUANTIZE_IMMEDIATE};
    bool enabled{true};
};

} // namespace CommRaT::Parameters