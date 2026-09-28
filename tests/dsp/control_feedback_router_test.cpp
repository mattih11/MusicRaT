#include <musicrat/dsp/control_feedback_router.hpp>

#include <cassert>
#include <limits>

int main() {
    using namespace CommRaT::Messages;
    using namespace CommRaT::Parameters;

    const CompiledControlFeedbackBinding bindings[] = {
        {
            .binding_id = 7,
            .target_parameter_id = 1,
            .destination_device_id = 3,
            .destination_endpoint_id = 11,
            .suppress_origin_id = 11,
        },
        {
            .binding_id = 8,
            .target_parameter_id = 1,
            .destination_device_id = 4,
            .destination_endpoint_id = 12,
            .suppress_origin_id = 12,
        },
    };

    musicrat::dsp::ControlFeedbackRouter router{};
    assert(router.configure(bindings, 2)
        == musicrat::dsp::ControlFeedbackRouterConfigError::None);

    ParameterStateBlock input{};
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.flags = PARAMETER_STATE_BLOCK_OVERFLOW;
    input.states.push_back({
        .parameter_id = 1,
        .source_endpoint_id = 11,
        .origin_id = 11,
        .binding_id = 7,
        .value = 0.5,
    });
    input.states.push_back({
        .parameter_id = 1,
        .value = 0.75,
    });
    input.states.push_back({
        .parameter_id = 1,
        .value = std::numeric_limits<double>::quiet_NaN(),
    });

    ControlFeedbackBlock output{};
    const auto result = router.process(input, output);

    assert(result.routed_count == 2);
    assert(result.suppressed_count == 1);
    assert(result.dropped_count == 0);
    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.flags == CONTROL_FEEDBACK_BLOCK_SOURCE_OVERFLOW);
    assert(output.events.size() == 2);
    assert(output.events[0].destination_device_id == 3);
    assert(output.events[0].destination_endpoint_id == 11);
    assert(output.events[0].binding_id == 7);
    assert(output.events[0].value == 0.75);
    assert(output.events[1].destination_device_id == 4);
    assert(output.events[1].destination_endpoint_id == 12);
    assert(output.events[1].binding_id == 8);
    assert(output.events[1].value == 0.75);

    auto invalid = bindings[0];
    invalid.suppress_origin_id = INVALID_CONTROL_ORIGIN_ID;
    assert(router.configure(&invalid, 1)
        == musicrat::dsp::ControlFeedbackRouterConfigError::InvalidBinding);
}
