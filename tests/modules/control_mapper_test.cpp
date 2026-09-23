#include <musicrat/modules/control_mapper.hpp>

#include <rfl/json.hpp>

#include <cassert>

namespace {

class TestControlMapper : public CommRaT::ControlMapper {
public:
    using CommRaT::ControlMapper::ControlMapper;
    using CommRaT::ControlMapper::process;
};

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "ControlMapperTest";
    config.outputs = commrat::MultiOutputConfig{.addresses = {{
        .system_id = 50,
        .instance_id = 1,
    }}};
    config.inputs = commrat::MultiInputConfig{
        .sources = {{
            .system_id = 40,
            .instance_id = 1,
            .lifecycle_address = 0,
            .is_primary = true,
        }},
    };
    config.params = rfl::json::read<rfl::Generic>(R"({
        "bindings": [{
            "binding_id": 7,
            "source_device_id": 3,
            "source_endpoint_id": 11,
            "target_parameter_id": 5,
            "source_kind": 0,
            "mode": 0,
            "curve": 0,
            "pickup": 0,
            "source_minimum": 0.0,
            "source_maximum": 1.0,
            "target_minimum": -1.0,
            "target_maximum": 1.0,
            "scale": 1.0,
            "offset": 0.0,
            "dead_zone": 0.0,
            "quantization": 0.0,
            "hysteresis": 0.0,
            "pickup_tolerance": 0.01,
            "initial_value": 0.0,
            "invert": false
        }]
    })").value();

    TestControlMapper mapper{config};
    CommRaT::Messages::ControlEventBlock input{};
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.events.push_back({
        .source_device_id = 3,
        .source_endpoint_id = 11,
        .origin_id = 13,
        .sample_offset = 2,
        .kind = CommRaT::Messages::CONTROL_UNIPOLAR,
        .value = 0.75,
    });

    commrat::Synced<CommRaT::Messages::ParameterStateBlock> state{};
    CommRaT::Messages::ParameterEventBlock output{};
    mapper.process(input, state, output);

    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.flags == 0);
    assert(output.events.size() == 1);
    assert(output.events[0].parameter_id == 5);
    assert(output.events[0].sample_offset == 2);
    assert(output.events[0].value == 0.5);
    assert(output.events[0].origin_id == 13);
    assert(output.events[0].binding_id == 7);
}