#include <musicrat/modules/control_feedback_router.hpp>

#include <rfl/json.hpp>

#include <cassert>

namespace {

class TestControlFeedbackRouter : public CommRaT::ControlFeedbackRouter {
public:
    using CommRaT::ControlFeedbackRouter::ControlFeedbackRouter;
    using CommRaT::ControlFeedbackRouter::process;
};

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "ControlFeedbackRouterTest";
    config.outputs = commrat::SimpleOutputConfig{
        .system_id = 50,
        .instance_id = 1,
    };
    config.inputs = commrat::SingleInputConfig{
        .source_system_id = 40,
        .source_instance_id = 1,
    };
    config.params = rfl::json::read<rfl::Generic>(R"({
        "bindings": [{
            "binding_id": 7,
            "target_parameter_id": 1,
            "destination_device_id": 3,
            "destination_endpoint_id": 11,
            "suppress_origin_id": 11
        }]
    })").value();

    TestControlFeedbackRouter router{config};
    CommRaT::Messages::ParameterStateBlock input{};
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.states.push_back({
        .parameter_id = 1,
        .origin_id = 13,
        .binding_id = 7,
        .value = 0.5,
    });
    CommRaT::Messages::ControlFeedbackBlock output{};

    router.process(input, output);

    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.flags == 0);
    assert(output.events.size() == 1);
    assert(output.events[0].destination_device_id == 3);
    assert(output.events[0].destination_endpoint_id == 11);
    assert(output.events[0].origin_id == 13);
    assert(output.events[0].binding_id == 7);
    assert(output.events[0].value == 0.5);
}
