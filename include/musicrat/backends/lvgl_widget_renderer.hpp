#pragma once

#include <musicrat/protocol/note_events.hpp>
#include <musicrat/protocol/ui_observation.hpp>

#include <lvgl.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace musicrat::backends {

template<std::size_t Capacity = 4>
class LvglWidgetUpdateQueue {
    static_assert(Capacity >= 2);

public:
    bool push(const CommRaT::Messages::WidgetUpdateBlock& block) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        blocks_[head] = block;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(CommRaT::Messages::WidgetUpdateBlock& block) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        block = blocks_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t increment(std::size_t index) noexcept {
        return (index + 1) % Capacity;
    }

    std::array<CommRaT::Messages::WidgetUpdateBlock, Capacity> blocks_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

template<std::size_t Capacity = CommRaT::Messages::ControlEventBlock::MAX_EVENTS + 1>
class LvglControlEventQueue {
    static_assert(Capacity >= 2);

public:
    bool push(const CommRaT::Messages::ControlEvent& event) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        events_[head] = event;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(CommRaT::Messages::ControlEvent& event) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        event = events_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t increment(std::size_t index) noexcept {
        return (index + 1) % Capacity;
    }

    std::array<CommRaT::Messages::ControlEvent, Capacity> events_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

template<std::size_t Capacity = CommRaT::Messages::NoteEventBlock::MAX_EVENTS + 1>
class LvglNoteEventQueue {
    static_assert(Capacity >= 2);

public:
    bool push(const CommRaT::Messages::NoteEvent& event) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        events_[head] = event;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(CommRaT::Messages::NoteEvent& event) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        event = events_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t increment(std::size_t index) noexcept {
        return (index + 1) % Capacity;
    }

    std::array<CommRaT::Messages::NoteEvent, Capacity> events_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

enum class LvglWidgetKind : uint8_t {
    Meter,
    Text,
    Toggle,
    Slider,
    Fader,
    Knob,
    Button,
    Choice,
    Keyboard,
};

struct LvglWidgetApplyResult {
    uint32_t applied_count{0};
    uint32_t missing_count{0};
    uint32_t incompatible_count{0};
};

struct LvglWidgetLayout {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
};

struct LvglWidgetChoice {
    double value{0.0};
    std::string label;
};

struct LvglWidgetDescription {
    std::string id;
    std::string kind;
    std::string display_name;
    LvglWidgetLayout layout;
    std::string route;
    std::optional<std::vector<LvglWidgetChoice>> choices;
};

struct LvglDisplayDescription {
    std::string instance_id;
    std::string surface_id;
    std::vector<LvglWidgetDescription> widgets;
};

struct LvglSurfaceManifest {
    uint32_t schema_version{0};
    std::string config_fingerprint;
    std::vector<LvglDisplayDescription> displays;
};

struct LvglSurfaceLoadResult {
    uint32_t created_count{0};
    uint32_t unsupported_count{0};
};

class LvglWidgetRenderer {
public:
    using InteractionHandler = void (*)(
        void* context,
        std::string_view widget_id,
        CommRaT::Messages::ControlEventKind kind,
        double value,
        uint16_t flags) noexcept;

    using NoteInteractionHandler = void (*)(
        void* context,
        std::string_view widget_id,
        uint32_t note_id,
        uint8_t key,
        float velocity,
        bool note_on) noexcept;

    void set_interaction_handler(
        InteractionHandler handler,
        void* context) noexcept {
        interaction_handler_ = handler;
        interaction_context_ = context;
    }

    void set_note_interaction_handler(
        NoteInteractionHandler handler,
        void* context) noexcept {
        note_interaction_handler_ = handler;
        note_interaction_context_ = context;
    }

    [[nodiscard]] LvglSurfaceLoadResult load_surface(
        const LvglDisplayDescription& display,
        lv_obj_t* parent,
        int32_t width,
        int32_t height);

    bool register_widget(
        std::string surface_id,
        std::string widget_id,
        LvglWidgetKind kind,
        lv_obj_t* object,
        std::vector<double> choice_values = {});

    [[nodiscard]] LvglWidgetApplyResult apply(
        const CommRaT::Messages::WidgetUpdateBlock& block);

private:
    struct KeyboardState;

    struct KeyboardKeyState {
        struct HeldContact {
            lv_indev_t* pointer{nullptr};
            bool active{false};
        };
        KeyboardState* keyboard{nullptr};
        uint8_t semitone{0};
        uint8_t active_key{0};
        uint32_t active_note_id{0};
        std::array<HeldContact, 16> contacts{};
    };

    struct KeyboardState {
        LvglWidgetRenderer* renderer{nullptr};
        std::string widget_id;
        int32_t octave{4};
        lv_obj_t* octave_label{nullptr};
        std::array<KeyboardKeyState, 12> keys{};
    };

    struct WidgetEntry {
        std::string surface_id;
        std::string widget_id;
        LvglWidgetKind kind;
        lv_obj_t* object;
        bool interactive;
        std::vector<double> choice_values;
    };

    static void handle_lvgl_event(lv_event_t* event);
    static void handle_keyboard_key_event(lv_event_t* event);
    static void handle_keyboard_octave_event(lv_event_t* event);
    void emit_interaction(lv_obj_t* object, lv_event_code_t code) noexcept;
    void emit_note(
        const KeyboardKeyState& key,
        bool note_on) noexcept;
    void update_keyboard_octave(KeyboardState& keyboard) noexcept;
    void initialize_keyboard(
        KeyboardState& keyboard,
        int32_t width,
        int32_t height) noexcept;

    std::vector<WidgetEntry> widgets_;
    std::vector<std::unique_ptr<KeyboardState>> keyboards_;
    InteractionHandler interaction_handler_{nullptr};
    void* interaction_context_{nullptr};
    NoteInteractionHandler note_interaction_handler_{nullptr};
    void* note_interaction_context_{nullptr};
    uint32_t next_note_id_{1};
    bool applying_update_{false};
};

} // namespace musicrat::backends