#include <musicrat/backends/lvgl_touch_input.hpp>
#include <musicrat/backends/lvgl_widget_renderer.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <vector>

namespace {

struct Note {
    uint32_t id;
    uint8_t key;
    bool on;
};

void capture(void* context, std::string_view, uint32_t id,
             uint8_t key, float, bool on) noexcept {
    static_cast<std::vector<Note>*>(context)->push_back({id, key, on});
}

void event(musicrat::backends::LvglTouchInput& input,
           uint16_t type, uint16_t code, int value = 0) {
    input_event value_event{};
    value_event.type = type;
    value_event.code = code;
    value_event.value = value;
    input.consume(value_event);
}

void touch(musicrat::backends::LvglTouchInput& input, int slot, int id, int x) {
    event(input, EV_ABS, ABS_MT_SLOT, slot);
    event(input, EV_ABS, ABS_MT_TRACKING_ID, id);
    event(input, EV_ABS, ABS_MT_POSITION_X, x * 2 + 10);
    event(input, EV_ABS, ABS_MT_POSITION_Y, 450);
}

void frame(musicrat::backends::LvglTouchInput& input) {
    event(input, EV_SYN, SYN_REPORT);
}

void release(musicrat::backends::LvglTouchInput& input, int slot) {
    event(input, EV_ABS, ABS_MT_SLOT, slot);
    event(input, EV_ABS, ABS_MT_TRACKING_ID, -1);
    frame(input);
}

} // namespace

int main() {
    using musicrat::backends::LvglTouchInput;
    lv_init();
    auto* display = lv_display_create(700, 400);
    assert(display != nullptr);
    const auto manifest = rfl::json::read<musicrat::backends::LvglSurfaceManifest>(R"({
        "schema_version":1,"config_fingerprint":"touch-test",
        "displays":[{"instance_id":"keys","surface_id":"main","widgets":[{
            "id":"keyboard","kind":"keyboard","display_name":"Keyboard",
            "layout":{"x":0,"y":0,"width":100,"height":100},"route":"control"
        }]}]
    })");
    assert(manifest);
    musicrat::backends::LvglWidgetRenderer renderer;
    std::vector<Note> notes;
    notes.reserve(64);
    renderer.set_note_interaction_handler(capture, &notes);
    assert(renderer.load_surface(manifest->displays[0], lv_screen_active(), 700, 400)
               .created_count == 1);
    input_absinfo x_axis{};
    x_axis.minimum = 10;
    x_axis.maximum = 1408;
    input_absinfo y_axis{};
    y_axis.minimum = 100;
    y_axis.maximum = 499;
    {
        LvglTouchInput input;
        assert(input.initialize(display, 6, x_axis, y_axis));
        touch(input, 0, 10, 50);
        touch(input, 1, 11, 250);
        touch(input, 2, 12, 450);
        assert(notes.empty());
        frame(input);
        assert(notes.size() == 3);
        assert(notes[0].on && notes[0].key == 60);
        assert(notes[1].on && notes[1].key == 64);
        assert(notes[2].on && notes[2].key == 67);
        assert(notes[0].id != notes[1].id && notes[1].id != notes[2].id);
        frame(input);
        assert(notes.size() == 3);
        release(input, 1);
        assert(notes.size() == 4 && !notes.back().on);
        assert(notes.back().id == notes[1].id);
        release(input, 0);
        release(input, 2);
        assert(notes.size() == 6);

        touch(input, 0, 20, 50);
        touch(input, 1, 21, 50);
        frame(input);
        assert(notes.size() == 7 && notes.back().on);
        const auto shared_id = notes.back().id;
        release(input, 0);
        assert(notes.size() == 7);
        release(input, 1);
        assert(notes.size() == 8 && !notes.back().on && notes.back().id == shared_id);

        touch(input, 0, 30, 50);
        frame(input);
        const auto previous_id = notes.back().id;
        touch(input, 0, 31, 250);
        frame(input);
        assert(notes.size() == 11);
        assert(!notes[9].on && notes[9].id == previous_id);
        assert(notes[10].on && notes[10].key == 64);
        event(input, EV_SYN, SYN_DROPPED);
        assert(notes.size() == 12 && !notes.back().on);
        touch(input, 0, 40, 450);
        frame(input);
        assert(notes.size() == 12);
        touch(input, 0, 41, 450);
        frame(input);
        assert(notes.size() == 13 && notes.back().on);
        touch(input, 99, 50, 50);
        frame(input);
        assert(notes.size() == 13);
    }
    assert(notes.size() == 14 && !notes.back().on);
    assert(notes.back().id == notes[12].id);
    lv_display_delete(display);
}
