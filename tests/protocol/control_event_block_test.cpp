#include <musicrat/protocol/control_events.hpp>

#include <sertial/message.hpp>

#include <cassert>

int main() {
    using namespace CommRaT::Messages;

    static_assert(ControlEventBlock::MAX_EVENTS > 0);

    ControlEventBlock original{};
    original.timestamp_ns = 123456789;
    original.sequence_number = 17;
    original.flags = CONTROL_EVENT_BLOCK_OVERFLOW;
    original.events.push_back(ControlEvent{
        .source_device_id = 3,
        .source_endpoint_id = 11,
        .origin_id = 23,
        .sample_offset = 32,
        .kind = CONTROL_RELATIVE,
        .value = -0.125,
        .flags = CONTROL_EVENT_GESTURE_BEGIN | CONTROL_EVENT_GESTURE_UPDATE,
    });

    const auto serialized = sertial::Message<ControlEventBlock>::serialize(original);
    const auto restored = sertial::Message<ControlEventBlock>::deserialize(
        serialized.view());

    assert(restored);
    assert(restored->timestamp_ns == original.timestamp_ns);
    assert(restored->sequence_number == original.sequence_number);
    assert(restored->flags == CONTROL_EVENT_BLOCK_OVERFLOW);
    assert(restored->events.size() == 1);
    assert(restored->events[0].source_device_id == 3);
    assert(restored->events[0].source_endpoint_id == 11);
    assert(restored->events[0].origin_id == 23);
    assert(restored->events[0].sample_offset == 32);
    assert(restored->events[0].kind == CONTROL_RELATIVE);
    assert(restored->events[0].value == -0.125);
    assert(restored->events[0].flags
        == (CONTROL_EVENT_GESTURE_BEGIN | CONTROL_EVENT_GESTURE_UPDATE));
}