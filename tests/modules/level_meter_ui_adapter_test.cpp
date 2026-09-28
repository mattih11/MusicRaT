#include <musicrat/modules/level_meter_ui_adapter.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <memory>

namespace {

class TestLevelMeterUiAdapter : public CommRaT::LevelMeterUiAdapter {
public:
    using CommRaT::LevelMeterUiAdapter::LevelMeterUiAdapter;
    using CommRaT::LevelMeterUiAdapter::process;
};

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "LevelMeterUiAdapterTest";
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
            "surface_id": "main",
            "widget_id": "peak-meter",
            "property": 0,
            "selector": 0,
            "channel": 0
        }]
    })").value();

    auto adapter = std::make_unique<TestLevelMeterUiAdapter>(config);
    CommRaT::Messages::LevelMeterBlock input{};
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.channel_count = 1;
    input.peak[0] = 0.75;

    CommRaT::Messages::WidgetUpdateBlock output{};
    adapter->process(input, output);

    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.flags == 0);
    assert(output.events.size() == 1);
    assert(output.events[0].binding_id == 7);
    assert(output.events[0].surface_id == "main");
    assert(output.events[0].widget_id == "peak-meter");
    assert(output.events[0].value == 0.75);
}
