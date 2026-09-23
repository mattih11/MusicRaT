#pragma once

#include <musicrat/config.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

using NoteEventType = uint8_t;

inline constexpr NoteEventType NOTE_ON = 0;
inline constexpr NoteEventType NOTE_OFF = 1;
inline constexpr NoteEventType NOTE_POLYPHONIC_PRESSURE = 2;
inline constexpr NoteEventType NOTE_PITCH_EXPRESSION = 3;
inline constexpr NoteEventType NOTE_TIMBRE_EXPRESSION = 4;

enum NoteEventBlockFlag : uint16_t {
    NOTE_EVENT_BLOCK_OVERFLOW = 1U << 0U,
};

struct NoteEvent {
    uint32_t source_endpoint_id{0};
    uint32_t note_id{0};
    uint32_t sample_offset{0};
    NoteEventType type{NOTE_ON};
    uint8_t channel{0};
    uint8_t key{0};
    float value{0.0F};
};

struct NoteEventBlock {
    static constexpr std::size_t MAX_EVENTS = musicrat::config::max_note_events;

    sertial::fixed_vector<NoteEvent, MAX_EVENTS> events{};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
    uint16_t flags{0};
};

} // namespace CommRaT::Messages