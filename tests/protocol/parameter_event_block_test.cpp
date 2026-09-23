#include <musicrat/protocol/parameter_events.hpp>

#include <sertial/message.hpp>

#include <cassert>

int main() {
    using CommRaT::Messages::ParameterEvent;
    using CommRaT::Messages::ParameterEventBlock;

    static_assert(ParameterEventBlock::MAX_EVENTS > 0);

    ParameterEventBlock original{};
    original.timestamp_ns = 123456789;
    original.sequence_number = 7;
    original.flags = CommRaT::Messages::PARAMETER_EVENT_BLOCK_SOURCE_OVERFLOW;
    original.events.push_back(ParameterEvent{
        .source_endpoint_id = 11,
        .parameter_id = 1,
        .sample_offset = 32,
        .value = 0.5,
        .origin_id = 23,
        .binding_id = 42,
    });

    const auto serialized = sertial::Message<ParameterEventBlock>::serialize(original);
    const auto restored = sertial::Message<ParameterEventBlock>::deserialize(serialized.view());

    assert(restored);
    assert(restored->timestamp_ns == original.timestamp_ns);
    assert(restored->sequence_number == original.sequence_number);
    assert(restored->flags
        == CommRaT::Messages::PARAMETER_EVENT_BLOCK_SOURCE_OVERFLOW);
    assert(restored->events.size() == 1);
    assert(restored->events[0].source_endpoint_id == 11);
    assert(restored->events[0].parameter_id == 1);
    assert(restored->events[0].sample_offset == 32);
    assert(restored->events[0].value == 0.5);
    assert(restored->events[0].origin_id == 23);
    assert(restored->events[0].binding_id == 42);
}