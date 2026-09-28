#include <musicrat/modules/control_source.hpp>

#include <rfl/json.hpp>

#include <cassert>

namespace {

class TestControlSource : public CommRaT::ControlSource {
public:
    using CommRaT::ControlSource::ControlSource;
    using CommRaT::ControlSource::process;
};

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "ControlSourceTest";
    config.outputs = commrat::SimpleOutputConfig{
        .system_id = 20,
        .instance_id = 1,
    };
    config.inputs = commrat::MultiInputConfig{
        .sources = {{
            .system_id = 30,
            .instance_id = 1,
            .lifecycle_address = 0,
            .is_primary = false,
        }},
    };
    config.period = std::chrono::milliseconds{10};
    config.params = rfl::json::read<rfl::Generic>(R"({
        "device_id": 3,
        "endpoint_id": 11,
        "origin_id": 23,
        "kind": 1,
        "value": -0.25,
        "gesture_flags": 3,
        "enabled": true
    })").value();

    TestControlSource source{config};
    commrat::Synced<CommRaT::Messages::ControlFeedbackBlock> feedback{};
    CommRaT::Messages::ControlEventBlock first{};
    CommRaT::Messages::ControlEventBlock second{};
    source.process(feedback, first);
    source.process(feedback, second);

    assert(first.events.size() == 1);
    assert(first.events[0].source_device_id == 3);
    assert(first.events[0].source_endpoint_id == 11);
    assert(first.events[0].origin_id == 23);
    assert(first.events[0].kind == CommRaT::Messages::CONTROL_BIPOLAR);
    assert(first.events[0].value == -0.25);
    assert(first.events[0].flags
        == (CommRaT::Messages::CONTROL_EVENT_GESTURE_BEGIN
            | CommRaT::Messages::CONTROL_EVENT_GESTURE_UPDATE));
    assert(first.sequence_number == 0);
    assert(second.sequence_number == 1);

    CommRaT::Messages::ControlFeedbackBlock external_feedback{};
    external_feedback.events.push_back({
        .destination_device_id = 3,
        .destination_endpoint_id = 11,
        .origin_id = 99,
        .binding_id = 7,
        .value = 0.75,
    });
    CommRaT::Messages::ControlEventBlock third{};
    source.process({external_feedback, true}, third);
    assert(source.has_feedback());
    assert(source.feedback_value() == 0.75);
    assert(third.events[0].value == -0.25);

    CommRaT::Messages::ControlFeedbackBlock reflexive_feedback{};
    reflexive_feedback.events.push_back({
        .destination_device_id = 3,
        .destination_endpoint_id = 11,
        .origin_id = 23,
        .binding_id = 7,
        .value = 1.0,
    });
    CommRaT::Messages::ControlEventBlock fourth{};
    source.process({reflexive_feedback, true}, fourth);
    assert(source.feedback_value() == 0.75);
    assert(fourth.events[0].value == -0.25);
}