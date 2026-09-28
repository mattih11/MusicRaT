#include <musicrat/backends/lvgl_widget_renderer.hpp>

#include <algorithm>
#include <cmath>
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
        } else {
            ++result.unsupported_count;
            continue;
        }

        lv_obj_set_pos(
            object,
            static_cast<int32_t>(std::lround(description.layout.x * width / 100.0)),
            static_cast<int32_t>(std::lround(description.layout.y * height / 100.0)));
        lv_obj_set_size(
            object,
            static_cast<int32_t>(std::lround(description.layout.width * width / 100.0)),
            static_cast<int32_t>(std::lround(description.layout.height * height / 100.0)));
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
        if (widgets_.back().interactive
            && (kind == LvglWidgetKind::Slider
                || kind == LvglWidgetKind::Fader
                || kind == LvglWidgetKind::Knob
                || kind == LvglWidgetKind::Toggle
                || kind == LvglWidgetKind::Button)) {
            lv_obj_add_event_cb(
                object, handle_lvgl_event, LV_EVENT_ALL, this);
        }
        ++result.created_count;
    }
    return result;
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