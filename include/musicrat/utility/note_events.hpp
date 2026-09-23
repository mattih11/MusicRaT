#pragma once

#include <musicrat/protocol/note_events.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace musicrat {

enum class NoteEventValidationError : uint8_t {
    None,
    Overflow,
    InvalidType,
    InvalidChannel,
    InvalidKey,
    InvalidValue,
    OffsetOutOfRange,
    UnsortedOffsets,
};

inline NoteEventValidationError validate_note_event_block(
    const CommRaT::Messages::NoteEventBlock& block,
    uint32_t frame_count) noexcept {
    if ((block.flags & CommRaT::Messages::NOTE_EVENT_BLOCK_OVERFLOW) != 0) {
        return NoteEventValidationError::Overflow;
    }

    uint32_t previous_offset = 0;
    for (std::size_t index = 0; index < block.events.size(); ++index) {
        const auto& event = block.events[index];
        if (event.channel >= 16) {
            return NoteEventValidationError::InvalidChannel;
        }
        if (event.key >= 128) {
            return NoteEventValidationError::InvalidKey;
        }
        if (!std::isfinite(event.value)) {
            return NoteEventValidationError::InvalidValue;
        }
        switch (event.type) {
        case CommRaT::Messages::NOTE_ON:
        case CommRaT::Messages::NOTE_OFF:
        case CommRaT::Messages::NOTE_POLYPHONIC_PRESSURE:
        case CommRaT::Messages::NOTE_TIMBRE_EXPRESSION:
            if (event.value < 0.0F || event.value > 1.0F) {
                return NoteEventValidationError::InvalidValue;
            }
            break;
        case CommRaT::Messages::NOTE_PITCH_EXPRESSION:
            if (event.value < -1.0F || event.value > 1.0F) {
                return NoteEventValidationError::InvalidValue;
            }
            break;
        default:
            return NoteEventValidationError::InvalidType;
        }
        if (event.sample_offset >= frame_count) {
            return NoteEventValidationError::OffsetOutOfRange;
        }
        if (index > 0 && event.sample_offset < previous_offset) {
            return NoteEventValidationError::UnsortedOffsets;
        }
        previous_offset = event.sample_offset;
    }
    return NoteEventValidationError::None;
}

} // namespace musicrat