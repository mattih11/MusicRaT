#include <musicrat/backends/lvgl_widget_renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace musicrat::backends {

LvglSurfaceLoadResult LvglWidgetRenderer::load_surface(
    const LvglDisplayDescription& display,
    lv_obj_t* parent,
    int32_t width,
    int32_t height) {
    LvglSurfaceLoadResult result{};
    if (parent == nullptr || width <= 0 || height <= 0) return result;

    for (const auto& description : display.widgets) {
        lv_obj_t* object = nullptr;
        LvglWidgetKind kind{};
        if (description.kind == "meter") {
            object = lv_bar_create(parent);
            lv_bar_set_range(object, 0, 100);
            kind = LvglWidgetKind::Meter;
        } else if (description.kind == "text" || description.kind == "display") {
            object = lv_label_create(parent);
            lv_label_set_text(object, description.display_name.c_str());
            kind = LvglWidgetKind::Text;
        } else if (description.kind == "toggle") {
            object = lv_switch_create(parent);
            kind = LvglWidgetKind::Toggle;
        } else if (description.kind == "slider") {
            object = lv_slider_create(parent);
            lv_slider_set_range(object, 0, 100);
            lv_slider_set_value(object, 50, LV_ANIM_OFF);
            kind = LvglWidgetKind::Slider;
        } else if (description.kind == "fader") {
            object = lv_slider_create(parent);
            lv_slider_set_range(object, 0, 100);
            lv_slider_set_orientation(object, LV_SLIDER_ORIENTATION_VERTICAL);
            kind = LvglWidgetKind::Fader;
        } else if (description.kind == "knob") {
            object = lv_arc_create(parent);
            lv_arc_set_range(object, 0, 100);
            lv_arc_set_rotation(object, 135);
            lv_arc_set_bg_angles(object, 0, 270);
            kind = LvglWidgetKind::Knob;
        } else if (description.kind == "button") {
            object = lv_button_create(parent);
            auto* label = lv_label_create(object);
            lv_label_set_text(label, description.display_name.c_str());
            lv_obj_center(label);
            kind = LvglWidgetKind::Button;
        } else if (description.kind == "choice") {
            object = lv_dropdown_create(parent);
            std::string options;
            for (const auto& choice : description.choices.value_or(
                    std::vector<LvglWidgetChoice>{})) {
                if (!options.empty()) options += '\n';
                options += choice.label;
            }
            if (options.empty()) options = description.display_name;
            lv_dropdown_set_options(object, options.c_str());
            kind = LvglWidgetKind::Choice;
        } else if (description.kind == "keyboard") {
            object = lv_obj_create(parent);
            kind = LvglWidgetKind::Keyboard;
        } else {
            ++result.unsupported_count;
            continue;
        }

        if (kind == LvglWidgetKind::Keyboard) {
            lv_obj_remove_style_all(object);
            lv_obj_set_scrollable(object, false);
            lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(object, 0, 0);
            lv_obj_set_style_pad_all(object, 0, 0);
        }
        lv_obj_set_pos(
            object,
            static_cast<int32_t>(std::lround(description.layout.x * width / 100.0)),
            static_cast<int32_t>(std::lround(description.layout.y * height / 100.0)));
        const auto object_width = static_cast<int32_t>(
            std::lround(description.layout.width * width / 100.0));
        const auto object_height = static_cast<int32_t>(
            std::lround(description.layout.height * height / 100.0));
        lv_obj_set_size(object, object_width, object_height);
        std::unique_ptr<KeyboardState> keyboard;
        if (kind == LvglWidgetKind::Keyboard) {
            keyboard = std::make_unique<KeyboardState>();
            keyboard->renderer = this;
            keyboard->widget_id = description.id;
        }
        std::vector<double> choice_values;
        if (description.choices) {
            choice_values.reserve(description.choices->size());
            for (const auto& choice : *description.choices) {
                choice_values.push_back(choice.value);
            }
        }
        if (!register_widget(
                display.surface_id,
                description.id,
                kind,
                object,
                std::move(choice_values))) {
            lv_obj_delete(object);
            ++result.unsupported_count;
            continue;
        }
        widgets_.back().interactive = description.route != "feedback";
        if (keyboard) {
            keyboards_.push_back(std::move(keyboard));
            initialize_keyboard(*keyboards_.back(), object_width, object_height);
        }
        if (widgets_.back().interactive
            && (kind == LvglWidgetKind::Slider
                || kind == LvglWidgetKind::Fader
                || kind == LvglWidgetKind::Knob
                || kind == LvglWidgetKind::Toggle
                || kind == LvglWidgetKind::Choice
                || kind == LvglWidgetKind::Button)) {
            lv_obj_add_event_cb(
                object, handle_lvgl_event, LV_EVENT_ALL, this);
        }
        ++result.created_count;
    }
    lv_obj_update_layout(parent);
    return result;
}

void LvglWidgetRenderer::initialize_keyboard(
    KeyboardState& keyboard,
    int32_t width,
    int32_t height) noexcept {
    const auto widget = std::find_if(
        widgets_.begin(), widgets_.end(), [&](const WidgetEntry& entry) {
            return entry.widget_id == keyboard.widget_id
                && entry.kind == LvglWidgetKind::Keyboard;
        });
    if (widget == widgets_.end()) return;

    auto* root = widget->object;
    if (width < 56 || height < 32) return;

    constexpr int32_t header_height = 22;
    auto* previous = lv_button_create(root);
    auto* octave_label = lv_label_create(root);
    auto* next = lv_button_create(root);
    const auto button_width = std::max<int32_t>(20, width / 10);
    lv_obj_set_size(previous, button_width, header_height);
    lv_obj_set_pos(previous, 0, 0);
    lv_label_set_text(lv_label_create(previous), "-");
    lv_obj_center(lv_obj_get_child(previous, 0));
    lv_obj_set_size(next, button_width, header_height);
    lv_obj_set_pos(next, width - button_width, 0);
    lv_label_set_text(lv_label_create(next), "+");
    lv_obj_center(lv_obj_get_child(next, 0));
    lv_obj_set_size(octave_label, width - 2 * button_width, header_height);
    lv_obj_set_pos(octave_label, button_width, 0);
    keyboard.octave_label = octave_label;
    update_keyboard_octave(keyboard);
    lv_obj_add_event_cb(
        previous, handle_keyboard_octave_event, LV_EVENT_CLICKED, &keyboard);
    lv_obj_add_event_cb(
        next, handle_keyboard_octave_event, LV_EVENT_CLICKED, &keyboard);

    const auto key_top = header_height;
    const auto key_height = height - key_top;
    const auto white_width = width / 7;
    constexpr std::array<uint8_t, 7> white_semitones{0, 2, 4, 5, 7, 9, 11};
    constexpr std::array<std::string_view, 7> white_labels{
        "C", "D", "E", "F", "G", "A", "B"};
    constexpr std::array<uint8_t, 5> black_semitones{1, 3, 6, 8, 10};
    constexpr std::array<uint8_t, 5> black_after_white{0, 1, 3, 4, 5};
    constexpr std::array<std::string_view, 5> black_labels{
        "C#", "D#", "F#", "G#", "A#"};

    for (std::size_t index = 0; index < white_semitones.size(); ++index) {
        auto* button = lv_button_create(root);
        const auto key_width = index + 1 == white_semitones.size()
            ? width - static_cast<int32_t>(index) * white_width
            : white_width;
        lv_obj_set_size(button, key_width, key_height);
        lv_obj_set_pos(button, static_cast<int32_t>(index) * white_width, key_top);
        lv_obj_set_style_bg_color(button, lv_color_white(), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(
            button, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(button, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_border_width(button, 1, 0);
        auto* label = lv_label_create(button);
        lv_label_set_text(label, white_labels[index].data());
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -4);
        auto& key = keyboard.keys[white_semitones[index]];
        key.keyboard = &keyboard;
        key.semitone = white_semitones[index];
        lv_obj_add_event_cb(
            button, handle_keyboard_key_event, LV_EVENT_ALL, &key);
    }

    const auto black_width = std::max<int32_t>(10, white_width * 3 / 5);
    const auto black_height = key_height * 3 / 5;
    for (std::size_t index = 0; index < black_semitones.size(); ++index) {
        auto* button = lv_button_create(root);
        const auto x = static_cast<int32_t>(black_after_white[index] + 1)
            * white_width - black_width / 2;
        lv_obj_set_size(button, black_width, black_height);
        lv_obj_set_pos(button, x, key_top);
        lv_obj_set_style_bg_color(button, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(
            button, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(button, 0, 0);
        auto* label = lv_label_create(button);
        lv_label_set_text(label, black_labels[index].data());
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -3);
        auto& key = keyboard.keys[black_semitones[index]];
        key.keyboard = &keyboard;
        key.semitone = black_semitones[index];
        lv_obj_add_event_cb(
            button, handle_keyboard_key_event, LV_EVENT_ALL, &key);
    }
}

bool LvglWidgetRenderer::register_widget(
    std::string surface_id,
    std::string widget_id,
    LvglWidgetKind kind,
    lv_obj_t* object,
    std::vector<double> choice_values) {
    if (surface_id.empty() || widget_id.empty() || object == nullptr) return false;
    const auto duplicate = std::find_if(
        widgets_.begin(), widgets_.end(), [&](const WidgetEntry& entry) {
            return entry.surface_id == surface_id && entry.widget_id == widget_id;
        });
    if (duplicate != widgets_.end()) return false;
    widgets_.push_back({
        .surface_id = std::move(surface_id),
        .widget_id = std::move(widget_id),
        .kind = kind,
        .object = object,
        .interactive = false,
        .choice_values = std::move(choice_values),
    });
    return true;
}

void LvglWidgetRenderer::handle_lvgl_event(lv_event_t* event) {
    auto* renderer = static_cast<LvglWidgetRenderer*>(
        lv_event_get_user_data(event));
    if (renderer == nullptr) return;
    renderer->emit_interaction(
        static_cast<lv_obj_t*>(lv_event_get_target(event)),
        lv_event_get_code(event));
}

void LvglWidgetRenderer::handle_keyboard_key_event(lv_event_t* event) {
    auto* key = static_cast<KeyboardKeyState*>(lv_event_get_user_data(event));
    if (key == nullptr || key->keyboard == nullptr) return;
    const auto code = lv_event_get_code(event);
    auto& keyboard = *key->keyboard;
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED
        && code != LV_EVENT_PRESS_LOST) return;
    auto* pointer = lv_event_get_indev(event);
    auto contact = std::find_if(
        key->contacts.begin(), key->contacts.end(), [pointer](const auto& held) {
            return held.active && held.pointer == pointer;
        });
    if (code == LV_EVENT_PRESSED) {
        if (contact != key->contacts.end()) return;
        contact = std::find_if(
            key->contacts.begin(), key->contacts.end(),
            [](const auto& held) { return !held.active; });
        if (contact == key->contacts.end()) {
            std::fprintf(stderr, "MusicRaT keyboard: contact capacity exceeded\n");
            return;
        }
        *contact = {.pointer = pointer, .active = true};
        if (key->active_note_id != 0) return;
        auto candidate = keyboard.renderer->next_note_id_++;
        if (candidate == 0) candidate = keyboard.renderer->next_note_id_++;
        key->active_note_id = candidate;
        const auto midi_key = 12 * (keyboard.octave + 1) + key->semitone;
        key->active_key = static_cast<uint8_t>(std::clamp(midi_key, 0, 127));
        keyboard.renderer->emit_note(*key, true);
    } else if (contact != key->contacts.end()) {
        contact->active = false;
        if (std::any_of(key->contacts.begin(), key->contacts.end(),
                        [](const auto& held) { return held.active; })) {
            lv_obj_add_state(
                static_cast<lv_obj_t*>(lv_event_get_target(event)), LV_STATE_PRESSED);
            return;
        }
        keyboard.renderer->emit_note(*key, false);
        key->active_note_id = 0;
    }
}

void LvglWidgetRenderer::handle_keyboard_octave_event(lv_event_t* event) {
    auto* keyboard = static_cast<KeyboardState*>(lv_event_get_user_data(event));
    if (keyboard == nullptr || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* label_object = target == nullptr ? nullptr : lv_obj_get_child(target, 0);
    const auto* label = label_object == nullptr
        ? nullptr
        : lv_label_get_text(label_object);
    if (label == nullptr) return;
    if (label[0] == '-' && keyboard->octave > 0) {
        --keyboard->octave;
    } else if (label[0] == '+' && keyboard->octave < 8) {
        ++keyboard->octave;
    }
    keyboard->renderer->update_keyboard_octave(*keyboard);
}

void LvglWidgetRenderer::emit_note(
    const KeyboardKeyState& key,
    bool note_on) noexcept {
    if (note_interaction_handler_ == nullptr || key.keyboard == nullptr) return;
    note_interaction_handler_(
        note_interaction_context_,
        key.keyboard->widget_id,
        key.active_note_id,
        key.active_key,
        note_on ? 1.0F : 0.0F,
        note_on);
}

void LvglWidgetRenderer::update_keyboard_octave(
    KeyboardState& keyboard) noexcept {
    if (keyboard.octave_label == nullptr) return;
    const auto label = std::string{"Octave "} + std::to_string(keyboard.octave);
    lv_label_set_text(keyboard.octave_label, label.c_str());
}

void LvglWidgetRenderer::emit_interaction(
    lv_obj_t* object,
    lv_event_code_t code) noexcept {
    using namespace CommRaT::Messages;

    if (applying_update_ || interaction_handler_ == nullptr) return;
    const auto widget = std::find_if(
        widgets_.begin(), widgets_.end(), [&](const WidgetEntry& entry) {
            return entry.object == object;
        });
    if (widget == widgets_.end() || !widget->interactive) return;

    ControlEventKind kind{};
    double value = 0.0;
    if (widget->kind == LvglWidgetKind::Slider
        || widget->kind == LvglWidgetKind::Fader) {
        kind = CONTROL_UNIPOLAR;
        value = lv_slider_get_value(object) / 100.0;
    } else if (widget->kind == LvglWidgetKind::Knob) {
        kind = CONTROL_UNIPOLAR;
        value = lv_arc_get_value(object) / 100.0;
    } else if (widget->kind == LvglWidgetKind::Toggle) {
        kind = CONTROL_BOOLEAN;
        value = lv_obj_has_state(object, LV_STATE_CHECKED) ? 1.0 : 0.0;
    } else if (widget->kind == LvglWidgetKind::Button) {
        kind = CONTROL_TRIGGER;
        value = code == LV_EVENT_RELEASED ? 0.0 : 1.0;
    } else if (widget->kind == LvglWidgetKind::Choice) {
        if (code != LV_EVENT_VALUE_CHANGED) return;
        const auto selected = lv_dropdown_get_selected(object);
        if (selected >= widget->choice_values.size()) return;
        kind = CONTROL_CHOICE;
        value = widget->choice_values[selected];
    } else {
        return;
    }

    uint16_t flags = 0;
    if (code == LV_EVENT_PRESSED) {
        flags = CONTROL_EVENT_GESTURE_BEGIN | CONTROL_EVENT_GESTURE_UPDATE;
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        flags = CONTROL_EVENT_GESTURE_UPDATE;
    } else if (code == LV_EVENT_RELEASED) {
        flags = CONTROL_EVENT_GESTURE_END;
    } else {
        return;
    }
    interaction_handler_(
        interaction_context_, widget->widget_id, kind, value, flags);
}

LvglWidgetApplyResult LvglWidgetRenderer::apply(
    const CommRaT::Messages::WidgetUpdateBlock& block) {
    LvglWidgetApplyResult result{};
    if ((block.flags & CommRaT::Messages::WIDGET_UPDATE_BLOCK_SOURCE_INVALID) != 0) {
        return result;
    }

    applying_update_ = true;
    for (const auto& update : block.events) {
        const auto widget = std::find_if(
            widgets_.begin(), widgets_.end(), [&](const WidgetEntry& entry) {
                return entry.surface_id == update.surface_id.c_str()
                    && entry.widget_id == update.widget_id.c_str();
            });
        if (widget == widgets_.end()) {
            ++result.missing_count;
            continue;
        }
        if (widget->interactive && lv_obj_has_state(widget->object, LV_STATE_PRESSED)
            && (widget->kind == LvglWidgetKind::Slider || widget->kind == LvglWidgetKind::Fader
                || widget->kind == LvglWidgetKind::Knob)) {
            continue;
        }

        if (update.property == CommRaT::Messages::WIDGET_PROPERTY_VALUE
            && (widget->kind == LvglWidgetKind::Meter
                || widget->kind == LvglWidgetKind::Slider
                || widget->kind == LvglWidgetKind::Fader
                || widget->kind == LvglWidgetKind::Knob)
            && update.kind == CommRaT::Messages::WIDGET_VALUE_CONTINUOUS
            && std::isfinite(update.value)) {
            const auto percent = static_cast<int32_t>(
                std::lround(std::clamp(update.value, 0.0, 1.0) * 100.0));
            if (widget->kind == LvglWidgetKind::Meter) {
                lv_bar_set_value(widget->object, percent, LV_ANIM_OFF);
            } else if (widget->kind == LvglWidgetKind::Slider
                || widget->kind == LvglWidgetKind::Fader) {
                lv_slider_set_value(widget->object, percent, LV_ANIM_OFF);
            } else {
                lv_arc_set_value(widget->object, percent);
            }
        } else if (update.property == CommRaT::Messages::WIDGET_PROPERTY_VALUE
            && widget->kind == LvglWidgetKind::Choice
            && update.kind == CommRaT::Messages::WIDGET_VALUE_CHOICE
            && std::isfinite(update.value)) {
            const auto selected = std::find(
                widget->choice_values.begin(), widget->choice_values.end(), update.value);
            if (selected == widget->choice_values.end()) {
                ++result.incompatible_count;
                continue;
            }
            lv_dropdown_set_selected(
                widget->object,
                static_cast<uint32_t>(selected - widget->choice_values.begin()));
        } else if (update.property == CommRaT::Messages::WIDGET_PROPERTY_ACTIVE
            && (widget->kind == LvglWidgetKind::Toggle
                || widget->kind == LvglWidgetKind::Button)
            && update.kind == CommRaT::Messages::WIDGET_VALUE_BOOLEAN) {
            const auto state = widget->kind == LvglWidgetKind::Button
                ? LV_STATE_PRESSED
                : LV_STATE_CHECKED;
            if (update.active) {
                lv_obj_add_state(widget->object, state);
            } else {
                lv_obj_remove_state(widget->object, state);
            }
        } else if (update.property == CommRaT::Messages::WIDGET_PROPERTY_TEXT
            && widget->kind == LvglWidgetKind::Text
            && update.kind == CommRaT::Messages::WIDGET_VALUE_TEXT) {
            lv_label_set_text(widget->object, update.text.c_str());
        } else {
            ++result.incompatible_count;
            continue;
        }
        ++result.applied_count;
    }
    applying_update_ = false;
    return result;
}

} // namespace musicrat::backends