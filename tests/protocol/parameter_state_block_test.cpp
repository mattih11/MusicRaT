#include <musicrat/protocol/parameter_state.hpp>

#include <sertial/message.hpp>

#include <cassert>

int main() {
    using namespace CommRaT::Messages;

    static_assert(ParameterStateBlock::MAX_STATES > 0);

    ParameterStateBlock original{};
    original.timestamp_ns = 987654321;
    original.sequence_number = 29;
    original.flags = PARAMETER_STATE_BLOCK_SNAPSHOT;
    original.states.push_back(ParameterState{
        .parameter_id = 7,
        .source_endpoint_id = 11,
        .origin_id = 23,
        .binding_id = 42,
        .value = 0.75,
    });

    const auto serialized = sertial::Message<ParameterStateBlock>::serialize(original);
    const auto restored = sertial::Message<ParameterStateBlock>::deserialize(
        serialized.view());

    assert(restored);
    assert(restored->timestamp_ns == original.timestamp_ns);
    assert(restored->sequence_number == original.sequence_number);
    assert(restored->flags == PARAMETER_STATE_BLOCK_SNAPSHOT);
    assert(restored->states.size() == 1);
    assert(restored->states[0].parameter_id == 7);
    assert(restored->states[0].source_endpoint_id == 11);
    assert(restored->states[0].origin_id == 23);
    assert(restored->states[0].binding_id == 42);
    assert(restored->states[0].value == 0.75);
}