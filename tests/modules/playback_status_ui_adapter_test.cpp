#include <musicrat/modules/playback_status_ui_adapter.hpp>

#include <rfl/json.hpp>
#include <sertial/message.hpp>

#include <cassert>
#include <memory>

namespace {
class TestAdapter : public CommRaT::PlaybackStatusUiAdapter {
public:
    using PlaybackStatusUiAdapter::PlaybackStatusUiAdapter;
    using PlaybackStatusUiAdapter::process;
};
}

int main() {
    commrat::ModuleConfig config{};
    config.name = "PlaybackUiTest";
    config.outputs = commrat::MultiOutputConfig{.addresses = {{
        .system_id = 50, .instance_id = 1,
    }}};
    config.inputs = commrat::SingleInputConfig{
        .source_system_id = 30, .source_instance_id = 1,
    };
    config.params = rfl::json::read<rfl::Generic>(R"({
        "bindings": [{
            "binding_id": 7, "surface_id": "deck", "widget_id": "elapsed",
            "property": 2, "selector": 10
        }]
    })").value();
    auto adapter = std::make_unique<TestAdapter>(config);
    CommRaT::Messages::PlaybackStatusBlock input{};
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.source_sample_rate_hz = 48000;
    input.media_position_frames = 72000;
    CommRaT::Messages::WidgetUpdateBlock output{};
    adapter->process(input, output);
    assert(output.flags == 0 && output.events.size() == 1);
    assert(output.events[0].binding_id == 7);
    assert(output.events[0].surface_id == "deck");
    assert(output.events[0].widget_id == "elapsed");
    assert(output.events[0].text == "00:01.500");
    const auto bytes = sertial::Message<CommRaT::Messages::WidgetUpdateBlock>::serialize(output);
    const auto restored = sertial::Message<CommRaT::Messages::WidgetUpdateBlock>::deserialize(bytes.view());
    assert(restored && restored->events[0].text == "00:01.500");
    assert(restored->timestamp_ns == input.timestamp_ns);

    const auto metadata = adapter->descriptor_metadata();
    assert(metadata.musicrat_ports.ports.size() == 2);
    assert(metadata.musicrat_ports.ports[1].id == "playback_status");

    config.params = rfl::json::read<rfl::Generic>(R"({
        "bindings": [{"binding_id": 0}]
    })").value();
    bool rejected = false;
    try {
        auto invalid = std::make_unique<TestAdapter>(config);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}
