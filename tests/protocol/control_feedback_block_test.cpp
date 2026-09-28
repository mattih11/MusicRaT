#include <musicrat/protocol/control_feedback.hpp>

#include <sertial/message.hpp>

#include <cassert>

int main() {
    using namespace CommRaT::Messages;

    static_assert(ControlFeedbackBlock::MAX_EVENTS > 0);

    ControlFeedbackBlock original{};
    original.timestamp_ns = 987654321;
    original.sequence_number = 31;
    original.flags = CONTROL_FEEDBACK_BLOCK_SOURCE_OVERFLOW;
    original.events.push_back({
        .destination_device_id = 3,
        .destination_endpoint_id = 11,
        .origin_id = 23,
        .binding_id = 42,
        .value = 0.75,
    });

    const auto serialized = sertial::Message<ControlFeedbackBlock>::serialize(original);
    const auto restored = sertial::Message<ControlFeedbackBlock>::deserialize(
        serialized.view());

    assert(restored);
    assert(restored->timestamp_ns == original.timestamp_ns);
    assert(restored->sequence_number == original.sequence_number);
    assert(restored->flags == original.flags);
    assert(restored->events.size() == 1);
    assert(restored->events[0].destination_device_id == 3);
    assert(restored->events[0].destination_endpoint_id == 11);
    assert(restored->events[0].origin_id == 23);
    assert(restored->events[0].binding_id == 42);
    assert(restored->events[0].value == 0.75);
}
