#include <musicrat/dsp/control_mapper.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using namespace CommRaT::Messages;
    using namespace musicrat::dsp;

    ControlMapper mapper{};
    const std::array bindings{
        CompiledControlBinding{
            .binding_id = 41,
            .source_device_id = 3,
            .source_endpoint_id = 11,
            .target_parameter_id = 7,
            .source_kind = CONTROL_UNIPOLAR,
            .mode = CONTROL_MAP_ABSOLUTE,
            .curve = CONTROL_CURVE_EXPONENTIAL,
            .target_minimum = -1.0,
            .target_maximum = 1.0,
            .initial_value = -1.0,
        },
        CompiledControlBinding{
            .binding_id = 42,
            .source_device_id = 3,
            .source_endpoint_id = 12,
            .target_parameter_id = 8,
            .source_kind = CONTROL_RELATIVE,
            .mode = CONTROL_MAP_RELATIVE,
            .target_minimum = 0.0,
            .target_maximum = 1.0,
            .scale = 0.1,
            .hysteresis = 0.05,
            .initial_value = 0.5,
        },
        CompiledControlBinding{
            .binding_id = 43,
            .source_device_id = 3,
            .source_endpoint_id = 13,
            .target_parameter_id = 9,
            .source_kind = CONTROL_BOOLEAN,
            .mode = CONTROL_MAP_TOGGLE,
            .target_minimum = 0.0,
            .target_maximum = 1.0,
        },
    };
    assert(mapper.configure(bindings) == ControlMapperConfigError::None);

    ControlEventBlock input{};
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.flags = CONTROL_EVENT_BLOCK_OVERFLOW;
    input.events.push_back({
        .source_device_id = 3,
        .source_endpoint_id = 11,
        .origin_id = 21,
        .sample_offset = 4,
        .kind = CONTROL_UNIPOLAR,
        .value = 0.5,
    });
    input.events.push_back({
        .source_device_id = 3,
        .source_endpoint_id = 12,
        .origin_id = 22,
        .sample_offset = 2,
        .kind = CONTROL_RELATIVE,
        .value = 0.25,
    });
    input.events.push_back({
        .source_device_id = 3,
        .source_endpoint_id = 13,
        .origin_id = 23,
        .sample_offset = 3,
        .kind = CONTROL_BOOLEAN,
        .value = 1.0,
    });

    ParameterEventBlock output{};
    auto result = mapper.process(input, 8, output);
    assert(result.mapped_count == 3);
    assert(result.dropped_count == 0);
    assert(result.invalid_count == 0);
    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.flags == PARAMETER_EVENT_BLOCK_SOURCE_OVERFLOW);
    assert(output.events.size() == 3);
    assert(output.events[0].parameter_id == 8);
    assert(output.events[0].sample_offset == 2);
    assert(output.events[0].value == 0.525);
    assert(output.events[1].parameter_id == 9);
    assert(output.events[1].sample_offset == 3);
    assert(output.events[1].value == 1.0);
    assert(output.events[2].parameter_id == 7);
    assert(output.events[2].sample_offset == 4);
    assert(output.events[2].value == -0.5);
    assert(output.events[2].origin_id == 21);
    assert(output.events[2].binding_id == 41);

    input.flags = 0;
    input.events.clear();
    input.events.push_back({
        .source_device_id = 3,
        .source_endpoint_id = 12,
        .kind = CONTROL_RELATIVE,
        .value = 0.25,
    });
    input.events.push_back({
        .source_device_id = 3,
        .source_endpoint_id = 13,
        .kind = CONTROL_BOOLEAN,
        .value = 1.0,
    });
    result = mapper.process(input, 8, output);
    assert(result.mapped_count == 0);
    assert(output.events.empty());

    input.events[0].value = 0.5;
    input.events[1].value = 0.0;
    input.events.push_back({
        .source_device_id = 3,
        .source_endpoint_id = 13,
        .kind = CONTROL_BOOLEAN,
        .value = 1.0,
    });
    result = mapper.process(input, 8, output);
    assert(result.mapped_count == 2);
    assert(std::abs(output.events[0].value - 0.6) < 1.0e-12);
    assert(output.events[1].value == 0.0);

    input.events.clear();
    input.events.push_back({
        .source_device_id = 3,
        .source_endpoint_id = 11,
        .sample_offset = 8,
        .kind = CONTROL_UNIPOLAR,
        .value = 0.5,
    });
    input.events.push_back({
        .source_device_id = 3,
        .source_endpoint_id = 11,
        .kind = CONTROL_UNIPOLAR,
        .value = std::numeric_limits<double>::quiet_NaN(),
    });
    result = mapper.process(input, 8, output);
    assert(result.invalid_count == 2);
    assert(output.events.empty());

    auto duplicate = bindings;
    duplicate[1].binding_id = duplicate[0].binding_id;
    assert(mapper.configure(duplicate)
        == ControlMapperConfigError::DuplicateBindingId);

    ControlMapper pickup_mapper{};
    const std::array pickup_bindings{
        CompiledControlBinding{
            .binding_id = 51,
            .source_device_id = 4,
            .source_endpoint_id = 14,
            .target_parameter_id = 10,
            .pickup = CONTROL_PICKUP_MATCH,
            .target_minimum = 0.0,
            .target_maximum = 1.0,
            .pickup_tolerance = 0.02,
        },
    };
    assert(pickup_mapper.configure(pickup_bindings)
        == ControlMapperConfigError::None);
    ParameterStateBlock state_block{};
    state_block.states.push_back({
        .parameter_id = 10,
        .binding_id = 51,
        .value = 0.8,
    });
    assert(pickup_mapper.synchronize(state_block) == 1);

    input.events.clear();
    input.events.push_back({
        .source_device_id = 4,
        .source_endpoint_id = 14,
        .kind = CONTROL_UNIPOLAR,
        .value = 0.2,
    });
    result = pickup_mapper.process(input, 8, output);
    assert(result.mapped_count == 0);
    input.events[0].value = 0.81;
    result = pickup_mapper.process(input, 8, output);
    assert(result.mapped_count == 1);
    assert(output.events[0].value == 0.81);

    ControlMapper overflow_mapper{};
    std::array<CompiledControlBinding, ControlMapper::MAX_BINDINGS>
        overflow_bindings{};
    for (std::size_t index = 0; index < overflow_bindings.size(); ++index) {
        overflow_bindings[index] = {
            .binding_id = static_cast<ControlBindingId>(index + 1),
            .source_device_id = 5,
            .source_endpoint_id = 15,
            .target_parameter_id = static_cast<uint32_t>(index + 1),
        };
    }
    assert(overflow_mapper.configure(overflow_bindings)
        == ControlMapperConfigError::None);
    input.events.clear();
    input.events.push_back({
        .source_device_id = 5,
        .source_endpoint_id = 15,
        .sample_offset = 2,
        .kind = CONTROL_UNIPOLAR,
        .value = 0.25,
    });
    input.events.push_back({
        .source_device_id = 5,
        .source_endpoint_id = 15,
        .sample_offset = 1,
        .kind = CONTROL_UNIPOLAR,
        .value = 0.75,
    });
    result = overflow_mapper.process(input, 8, output);
    assert(result.mapped_count == ParameterEventBlock::MAX_EVENTS);
    assert(result.dropped_count == ParameterEventBlock::MAX_EVENTS);
    assert(output.events.size() == ParameterEventBlock::MAX_EVENTS);
    assert((output.flags & PARAMETER_EVENT_BLOCK_OVERFLOW) != 0);
}