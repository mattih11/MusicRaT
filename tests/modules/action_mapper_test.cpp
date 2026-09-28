#include <musicrat/modules/action_mapper.hpp>

#include <rfl/json.hpp>

#include <cassert>

namespace {

class TestActionMapper : public CommRaT::ActionMapper {
public:
    using CommRaT::ActionMapper::ActionMapper;
    using CommRaT::ActionMapper::process;
};

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "ActionMapperTest";
    config.outputs = commrat::MultiOutputConfig{.addresses = {{
        .system_id = 50,
        .instance_id = 1,
    }}};
    config.inputs = commrat::SingleInputConfig{
        .source_system_id = 40,
        .source_instance_id = 1,
        .source_lifecycle_address = 0,
    };
    config.params = rfl::json::read<rfl::Generic>(R"({
        "bindings": [{
            "binding_id": 7,
            "source_device_id": 3,
            "source_endpoint_id": 11,
            "source_kind": 0,
            "action_type": 2,
            "quantization": 1,
            "source_minimum": 0.0,
            "source_maximum": 1.0,
            "target_minimum": 0.5,
            "target_maximum": 2.0,
            "default_value": 1.0,
            "ramp_frames": 64
        }]
    })").value();

    TestActionMapper mapper{config};
    CommRaT::Messages::ControlEventBlock input{};
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.events.push_back({
        .source_device_id = 3,
        .source_endpoint_id = 11,
        .sample_offset = 2,
        .kind = CommRaT::Messages::CONTROL_UNIPOLAR,
        .value = 0.5,
    });

    CommRaT::Messages::DeckControlEventBlock output{};
    mapper.process(input, output);

    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.events.size() == 1);
    assert(output.events[0].type == CommRaT::Messages::DECK_CONTROL_SET_RATE);
    assert(output.events[0].sample_offset == 2);
    assert(output.events[0].value == 1.25);
    assert(output.events[0].ramp_frames == 64);
    assert(output.events[0].quantization == CommRaT::Messages::DECK_QUANTIZE_BEAT);
}
