#include <musicrat/backends/lvgl_widget_renderer.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <string_view>

namespace {

struct InteractionCapture {
    std::string widget_id;
    CommRaT::Messages::ControlEventKind kind{0};
    double value{0.0};
    uint16_t flags{0};
    uint32_t count{0};
};

void capture_interaction(
    void* context,
    std::string_view widget_id,
    CommRaT::Messages::ControlEventKind kind,
    double value,
    uint16_t flags) noexcept {
    auto& capture = *static_cast<InteractionCapture*>(context);
    capture.widget_id = widget_id;
    capture.kind = kind;
    capture.value = value;
    capture.flags = flags;
    ++capture.count;
}

} // namespace

int main() {
    using namespace CommRaT::Messages;
    using musicrat::backends::LvglWidgetRenderer;
    using musicrat::backends::LvglWidgetUpdateQueue;

    LvglWidgetUpdateQueue<3> queue;
    WidgetUpdateBlock queued_first{};
    queued_first.sequence_number = 1;
    WidgetUpdateBlock queued_second{};
    queued_second.sequence_number = 2;
    assert(queue.push(queued_first));
    assert(queue.push(queued_second));
    assert(!queue.push(queued_second));

    WidgetUpdateBlock dequeued{};
    assert(queue.pop(dequeued));
    assert(dequeued.sequence_number == 1);
    assert(queue.pop(dequeued));
    assert(dequeued.sequence_number == 2);
    assert(!queue.pop(dequeued));

    musicrat::backends::LvglControlEventQueue<3> control_queue;
    ControlEvent control_event{.source_device_id = 1, .source_endpoint_id = 2};
    assert(control_queue.push(control_event));
    assert(control_queue.pop(control_event));
    assert(control_event.source_endpoint_id == 2);

    lv_init();
    auto* display = lv_display_create(320, 240);
    assert(display != nullptr);

    auto* screen = lv_obj_create(nullptr);
    LvglWidgetRenderer renderer;
    InteractionCapture interaction{};
    renderer.set_interaction_handler(capture_interaction, &interaction);
    const auto manifest = rfl::json::read<musicrat::backends::LvglSurfaceManifest>(R"({
        "schema_version": 1,
        "config_fingerprint": "fnv1a32:12345678",
        "observation_bindings": [],
        "displays": [{
            "instance_id": "display-1",
            "surface_id": "main",
            "widgets": [
                {"id":"meter","kind":"meter","display_name":"Peak","layout":{"x":5,"y":5,"width":10,"height":50},"route":"feedback","observation_binding_id":"peak"},
                {"id":"clip","kind":"toggle","display_name":"Clip","layout":{"x":20,"y":5,"width":15,"height":12},"route":"feedback"},
                {"id":"status","kind":"text","display_name":"Status","layout":{"x":40,"y":5,"width":25,"height":12},"route":"feedback"},
                {"id":"slider","kind":"slider","display_name":"Level","layout":{"x":5,"y":60,"width":35,"height":12},"route":"bidirectional"},
                {"id":"fader","kind":"fader","display_name":"Volume","layout":{"x":42,"y":45,"width":10,"height":45},"route":"bidirectional"},
                {"id":"knob","kind":"knob","display_name":"Tone","layout":{"x":45,"y":55,"width":18,"height":24},"route":"bidirectional"},
                {"id":"button","kind":"button","display_name":"Play","layout":{"x":68,"y":60,"width":20,"height":12},"route":"control"},
                {"id":"choice","kind":"choice","display_name":"Mode","layout":{"x":68,"y":78,"width":25,"height":12},"route":"bidirectional","choices":[{"value":10,"label":"Clean"},{"value":20,"label":"Drive"}]}
            ]
        }]
    })");
    assert(manifest);
    const auto loaded = renderer.load_surface(manifest->displays[0], screen, 320, 240);
    assert(loaded.created_count == 8);
    assert(loaded.unsupported_count == 0);

    auto* meter = lv_obj_get_child(screen, 0);
    auto* toggle = lv_obj_get_child(screen, 1);
    auto* text = lv_obj_get_child(screen, 2);
    auto* slider = lv_obj_get_child(screen, 3);
    auto* fader = lv_obj_get_child(screen, 4);
    auto* knob = lv_obj_get_child(screen, 5);
    auto* button = lv_obj_get_child(screen, 6);
    auto* choice = lv_obj_get_child(screen, 7);
    assert(meter != nullptr);
    assert(toggle != nullptr);
    assert(text != nullptr);
    assert(slider != nullptr);
    assert(fader != nullptr);
    assert(lv_slider_get_orientation(fader) == LV_SLIDER_ORIENTATION_VERTICAL);
    assert(knob != nullptr);
    assert(button != nullptr);
    assert(choice != nullptr);

    WidgetUpdateBlock block{};
    block.events.push_back({
        .binding_id = 1,
        .surface_id = "main",
        .widget_id = "meter",
        .property = WIDGET_PROPERTY_VALUE,
        .kind = WIDGET_VALUE_CONTINUOUS,
        .value = 0.75,
    });
    block.events.push_back({
        .binding_id = 2,
        .surface_id = "main",
        .widget_id = "clip",
        .property = WIDGET_PROPERTY_ACTIVE,
        .kind = WIDGET_VALUE_BOOLEAN,
        .active = true,
    });
    block.events.push_back({
        .binding_id = 3,
        .surface_id = "main",
        .widget_id = "status",
        .property = WIDGET_PROPERTY_TEXT,
        .kind = WIDGET_VALUE_TEXT,
        .text = "Ready",
    });
    block.events.push_back({
        .binding_id = 4,
        .surface_id = "main",
        .widget_id = "slider",
        .property = WIDGET_PROPERTY_VALUE,
        .kind = WIDGET_VALUE_CONTINUOUS,
        .value = 0.4,
    });
    block.events.push_back({
        .binding_id = 5,
        .surface_id = "main",
        .widget_id = "fader",
        .property = WIDGET_PROPERTY_VALUE,
        .kind = WIDGET_VALUE_CONTINUOUS,
        .value = 0.6,
    });
    block.events.push_back({
        .binding_id = 6,
        .surface_id = "main",
        .widget_id = "knob",
        .property = WIDGET_PROPERTY_VALUE,
        .kind = WIDGET_VALUE_CONTINUOUS,
        .value = 1.2,
    });
    block.events.push_back({
        .binding_id = 7,
        .surface_id = "main",
        .widget_id = "button",
        .property = WIDGET_PROPERTY_ACTIVE,
        .kind = WIDGET_VALUE_BOOLEAN,
        .active = true,
    });
    block.events.push_back({
        .binding_id = 8,
        .surface_id = "main",
        .widget_id = "choice",
        .property = WIDGET_PROPERTY_VALUE,
        .kind = WIDGET_VALUE_CHOICE,
        .value = 20,
    });
    block.events.push_back({
        .binding_id = 9,
        .surface_id = "main",
        .widget_id = "missing",
        .property = WIDGET_PROPERTY_VALUE,
        .kind = WIDGET_VALUE_CONTINUOUS,
        .value = 0.5,
    });

    const auto result = renderer.apply(block);
    assert(result.applied_count == 8);
    assert(result.missing_count == 1);
    assert(result.incompatible_count == 0);
    assert(lv_bar_get_value(meter) == 75);
    assert(lv_obj_has_state(toggle, LV_STATE_CHECKED));
    assert(std::string_view{lv_label_get_text(text)} == "Ready");
    assert(lv_slider_get_value(slider) == 40);
    assert(lv_slider_get_value(fader) == 60);
    assert(lv_arc_get_value(knob) == 100);
    assert(lv_obj_has_state(button, LV_STATE_PRESSED));
    assert(lv_dropdown_get_selected(choice) == 1);

    lv_slider_set_value(slider, 65, LV_ANIM_OFF);
    lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(interaction.count == 1);
    assert(interaction.widget_id == "slider");
    assert(interaction.kind == CONTROL_UNIPOLAR);
    assert(interaction.value == 0.65);
    assert(interaction.flags == CONTROL_EVENT_GESTURE_UPDATE);

    lv_slider_set_value(fader, 20, LV_ANIM_OFF);
    lv_obj_send_event(fader, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(interaction.count == 2);
    assert(interaction.widget_id == "fader");
    assert(interaction.kind == CONTROL_UNIPOLAR);
    assert(interaction.value == 0.2);
    assert(interaction.flags == CONTROL_EVENT_GESTURE_UPDATE);

    lv_arc_set_value(knob, 35);
    lv_obj_send_event(knob, LV_EVENT_RELEASED, nullptr);
    assert(interaction.count == 3);
    assert(interaction.widget_id == "knob");
    assert(interaction.value == 0.35);
    assert(interaction.flags == CONTROL_EVENT_GESTURE_END);

    block.flags = WIDGET_UPDATE_BLOCK_SOURCE_INVALID;
    block.events[0].value = 0.25;
    assert(renderer.apply(block).applied_count == 0);
    assert(lv_bar_get_value(meter) == 75);

    lv_obj_delete(screen);
    lv_display_delete(display);
}