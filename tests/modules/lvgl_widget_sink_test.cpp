#include <musicrat/modules/lvgl_widget_sink.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

namespace {

class TestLvglWidgetSink : public CommRaT::LvglWidgetSink {
public:
    using CommRaT::LvglWidgetSink::LvglWidgetSink;
    using CommRaT::LvglWidgetSink::on_disable;
    using CommRaT::LvglWidgetSink::on_enable;
    using CommRaT::LvglWidgetSink::process;
    using CommRaT::LvglWidgetSink::queue_interaction;
};

} // namespace

int main() {
    const auto manifest_path =
        std::filesystem::temp_directory_path() / "musicrat-lvgl-sink-test.json";
    {
        std::ofstream manifest{manifest_path};
        manifest << R"({
            "schema_version": 1,
            "config_fingerprint": "test",
            "displays": [{
                "instance_id": "display-1",
                "surface_id": "main",
                "widgets": [{
                    "id": "meter",
                    "kind": "meter",
                    "display_name": "Peak",
                    "layout": {"x": 5, "y": 5, "width": 20, "height": 80},
                    "route": "feedback"
                }]
            }]
        })";
    }

    commrat::ModuleConfig config{};
    config.name = "LvglWidgetSinkTest";
    config.outputs = commrat::SimpleOutputConfig{.system_id = 51, .instance_id = 1};
    config.inputs = commrat::MultiInputConfig{
        .sources = {{
            .system_id = 50,
            .instance_id = 1,
            .lifecycle_address = 0,
            .is_primary = false,
        }},
    };
    config.period = std::chrono::milliseconds{10};
    config.params = rfl::json::read<rfl::Generic>(std::string{
        "{\"manifest_path\":\"" + manifest_path.string()
        + "\",\"display_instance_id\":\"display-1\","
                    "\"width\":320,\"height\":240,\"device_id\":5,"
                    "\"control_bindings\":[{\"widget_id\":\"gain\","
                    "\"endpoint_id\":7,\"origin_id\":7,\"kind\":0}]}"}).value();

    auto sink = std::make_unique<TestLvglWidgetSink>(config);
    assert(sink->on_enable() == commrat::LifecycleResult::Success);
    CommRaT::Messages::WidgetUpdateBlock block{};
    block.sequence_number = 1;
    block.events.push_back({
        .binding_id = 1,
        .surface_id = "main",
        .widget_id = "meter",
        .property = CommRaT::Messages::WIDGET_PROPERTY_VALUE,
        .kind = CommRaT::Messages::WIDGET_VALUE_CONTINUOUS,
        .value = 0.75,
    });
    CommRaT::Messages::ControlEventBlock controls{};
    sink->process(
        commrat::Synced<CommRaT::Messages::WidgetUpdateBlock>{block, true},
        controls);
    assert(controls.events.empty());

    sink->queue_interaction(
        "gain",
        CommRaT::Messages::CONTROL_UNIPOLAR,
        0.6,
        CommRaT::Messages::CONTROL_EVENT_GESTURE_UPDATE);
    sink->process({}, controls);
    assert(controls.events.size() == 1);
    assert(controls.events[0].source_device_id == 5);
    assert(controls.events[0].source_endpoint_id == 7);
    assert(controls.events[0].origin_id == 7);
    assert(controls.events[0].value == 0.6);

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds{1};
    while (sink->blocks_applied() != 1
        && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    assert(sink->blocks_queued() == 1);
    assert(sink->blocks_dropped() == 0);
    assert(sink->blocks_applied() == 1);

    sink->on_disable();
    std::filesystem::remove(manifest_path);
}