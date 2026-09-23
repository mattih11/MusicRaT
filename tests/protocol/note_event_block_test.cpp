#include <musicrat/utility/note_events.hpp>

#include <cassert>
#include <limits>

int main() {
    using CommRaT::Messages::NoteEvent;
    using CommRaT::Messages::NoteEventBlock;
    using musicrat::NoteEventValidationError;

    NoteEventBlock block{};
    block.timestamp_ns = 1234;
    block.sequence_number = 9;
    block.events.push_back(NoteEvent{
        .source_endpoint_id = 3,
        .note_id = 17,
        .sample_offset = 4,
        .type = CommRaT::Messages::NOTE_ON,
        .channel = 2,
        .key = 60,
        .value = 0.75F,
    });
    block.events.push_back(NoteEvent{
        .source_endpoint_id = 3,
        .note_id = 17,
        .sample_offset = 4,
        .type = CommRaT::Messages::NOTE_OFF,
        .channel = 2,
        .key = 60,
        .value = 0.25F,
    });

    assert(musicrat::validate_note_event_block(block, 8)
        == NoteEventValidationError::None);
    assert(block.events[0].note_id == block.events[1].note_id);

    block.events[1].sample_offset = 3;
    assert(musicrat::validate_note_event_block(block, 8)
        == NoteEventValidationError::UnsortedOffsets);
    block.events[1].sample_offset = 4;

    block.events[0].value = std::numeric_limits<float>::quiet_NaN();
    assert(musicrat::validate_note_event_block(block, 8)
        == NoteEventValidationError::InvalidValue);
    block.events[0].value = 0.75F;

    block.events[0].type = CommRaT::Messages::NOTE_PITCH_EXPRESSION;
    block.events[0].value = -0.5F;
    assert(musicrat::validate_note_event_block(block, 8)
        == NoteEventValidationError::None);
    block.events[0].value = -1.5F;
    assert(musicrat::validate_note_event_block(block, 8)
        == NoteEventValidationError::InvalidValue);
    block.events[0].type = CommRaT::Messages::NOTE_ON;
    block.events[0].value = 0.75F;

    block.events[0].type = 255;
    assert(musicrat::validate_note_event_block(block, 8)
        == NoteEventValidationError::InvalidType);
    block.events[0].type = CommRaT::Messages::NOTE_ON;

    block.events[1].sample_offset = 8;
    assert(musicrat::validate_note_event_block(block, 8)
        == NoteEventValidationError::OffsetOutOfRange);
    block.events[1].sample_offset = 4;

    block.flags = CommRaT::Messages::NOTE_EVENT_BLOCK_OVERFLOW;
    assert(musicrat::validate_note_event_block(block, 8)
        == NoteEventValidationError::Overflow);
}