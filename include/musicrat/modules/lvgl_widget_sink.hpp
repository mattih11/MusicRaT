#pragma once

#include <musicrat/backends/lvgl_display_backend.hpp>
#include <musicrat/backends/lvgl_widget_renderer.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

#include <rfl/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace CommRaT {

class LvglWidgetSink : public MusicRaT::Module2<
    commrat::Output<Messages::ControlEventBlock>,
    commrat::SyncedInput<Messages::WidgetUpdateBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::LvglWidgetSink>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::ControlEventBlock>,
        commrat::SyncedInput<Messages::WidgetUpdateBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::LvglWidgetSink>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {
            {
                .id = "control_events",
                .display_name = "Control Events",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_CONTROL,
            },
            {
                .id = "widget_updates",
                .display_name = "Widget Updates",
                .direction = PORT_DIRECTION_SYNCED_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_TELEMETRY,
                .required = false,
            },
        };
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::LVGL_WIDGET_SINK_MANIFEST_PATH_PARAMETER_ID,
                .name = "manifest_path",
                .display_name = "LVGL Manifest",
                .group = "Surface",
                .kind = PARAMETER_KIND_TEXT,
            },
            {
                .id = Parameters::LVGL_WIDGET_SINK_DISPLAY_INSTANCE_PARAMETER_ID,
                .name = "display_instance_id",
                .display_name = "Display Instance",
                .group = "Surface",
                .kind = PARAMETER_KIND_TEXT,
            },
            {
                .id = Parameters::LVGL_WIDGET_SINK_WIDTH_PARAMETER_ID,
                .name = "width",
                .display_name = "Width",
                .group = "Display",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "px",
                .minimum = 1.0,
                .maximum = 16384.0,
                .step = 1.0,
            },
            {
                .id = Parameters::LVGL_WIDGET_SINK_HEIGHT_PARAMETER_ID,
                .name = "height",
                .display_name = "Height",
                .group = "Display",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "px",
                .minimum = 1.0,
                .maximum = 16384.0,
                .step = 1.0,
            },
            {
                .id = Parameters::LVGL_WIDGET_SINK_BACKEND_PARAMETER_ID,
                .name = "backend",
                .display_name = "Display Backend",
                .group = "Display",
                .kind = PARAMETER_KIND_CHOICE,
                .choices = {
                    {.value = Parameters::LVGL_DISPLAY_BACKEND_HEADLESS, .label = "Headless"},
                    {.value = Parameters::LVGL_DISPLAY_BACKEND_DRM, .label = "DRM/KMS"},
                    {.value = Parameters::LVGL_DISPLAY_BACKEND_FRAMEBUFFER, .label = "Framebuffer"},
                    {.value = Parameters::LVGL_DISPLAY_BACKEND_SDL, .label = "SDL Window"},
                },
            },
            {
                .id = Parameters::LVGL_WIDGET_SINK_DEVICE_PATH_PARAMETER_ID,
                .name = "device_path",
                .display_name = "Display Device",
                .group = "Display",
                .kind = PARAMETER_KIND_TEXT,
            },
            {
                .id = Parameters::LVGL_WIDGET_SINK_CONNECTOR_ID_PARAMETER_ID,
                .name = "connector_id",
                .display_name = "DRM Connector ID",
                .group = "Display",
                .kind = PARAMETER_KIND_INTEGER,
                .minimum = -1.0,
                .maximum = 4294967295.0,
                .step = 1.0,
            },
            {
                .id = Parameters::LVGL_WIDGET_SINK_WINDOW_TITLE_PARAMETER_ID,
                .name = "window_title",
                .display_name = "Window Title",
                .group = "Display",
                .kind = PARAMETER_KIND_TEXT,
            },
            {
                .id = Parameters::LVGL_WIDGET_SINK_DEVICE_ID_PARAMETER_ID,
                .name = "device_id",
                .display_name = "Surface Device ID",
                .group = "Control",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "",
                .minimum = 0.0,
                .maximum = 4294967295.0,
                .step = 1.0,
                .choices = {},
            },
        };
        return metadata;
    }

    explicit LvglWidgetSink(const commrat::ModuleConfig& config)
        : Base(config) {}

    [[nodiscard]] uint64_t blocks_queued() const noexcept {
        return blocks_queued_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t blocks_dropped() const noexcept {
        return blocks_dropped_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t blocks_applied() const noexcept {
        return blocks_applied_.load(std::memory_order_relaxed);
    }

protected:
    commrat::LifecycleResult on_enable() override {
        if (this->params_.manifest_path.empty()
            || this->params_.width == 0
            || this->params_.height == 0) {
            return commrat::LifecycleResult::Failed;
        }

        ui_state_.store(UiState::Starting, std::memory_order_release);
        ui_thread_ = std::jthread([this](std::stop_token stop_token) {
            ui_loop(stop_token);
        });
        auto state = ui_state_.load(std::memory_order_acquire);
        while (state == UiState::Starting) {
            ui_state_.wait(state, std::memory_order_acquire);
            state = ui_state_.load(std::memory_order_acquire);
        }
        if (state == UiState::Running) {
            return commrat::LifecycleResult::Success;
        }
        ui_thread_.join();
        return commrat::LifecycleResult::Failed;
    }

    void on_disable() override {
        if (!ui_thread_.joinable()) return;
        ui_thread_.request_stop();
        ui_thread_.join();
    }

    void process(
        const commrat::Synced<Messages::WidgetUpdateBlock>& input,
        Messages::ControlEventBlock& output) override {
        if (input.is_fresh()) {
            if (widget_updates_.push(input.value())) {
                blocks_queued_.fetch_add(1, std::memory_order_relaxed);
            } else {
                blocks_dropped_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        output.events.clear();
        output.timestamp_ns = commrat::Time::now();
        output.sequence_number = sequence_number_++;
        output.flags = control_overflow_.exchange(false, std::memory_order_relaxed)
            ? static_cast<uint16_t>(Messages::CONTROL_EVENT_BLOCK_OVERFLOW)
            : uint16_t{0};
        Messages::ControlEvent event{};
        while (output.events.size() < Messages::ControlEventBlock::MAX_EVENTS
            && control_events_.pop(event)) {
            output.events.push_back(event);
        }
    }

    void queue_interaction(
        std::string_view widget_id,
        Messages::ControlEventKind kind,
        double value,
        uint16_t flags) noexcept {
        const auto binding = std::find_if(
            this->params_.control_bindings.begin(),
            this->params_.control_bindings.end(),
            [&](const auto& candidate) {
                return candidate.widget_id == widget_id;
            });
        if (binding == this->params_.control_bindings.end()
            || binding->kind != kind
            || this->params_.device_id == Messages::INVALID_CONTROL_DEVICE_ID) {
            return;
        }
        if (!control_events_.push({
                .source_device_id = this->params_.device_id,
                .source_endpoint_id = binding->endpoint_id,
                .origin_id = binding->origin_id,
                .sample_offset = 0,
                .kind = kind,
                .value = value,
                .flags = flags,
            })) {
            control_overflow_.store(true, std::memory_order_relaxed);
        }
    }

private:
    enum class UiState : uint8_t {
        Stopped,
        Starting,
        Running,
        Failed,
    };

    bool drain_one(musicrat::backends::LvglWidgetRenderer& renderer) {
        Messages::WidgetUpdateBlock block{};
        if (!widget_updates_.pop(block)) return false;
        (void)renderer.apply(block);
        blocks_applied_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void ui_loop(std::stop_token stop_token) noexcept {
        try {
            const auto manifest = rfl::json::load<
                musicrat::backends::LvglSurfaceManifest>(
                    this->params_.manifest_path.c_str());
            if (!manifest || manifest->displays.empty()) {
                set_ui_state(UiState::Failed);
                return;
            }
            const auto requested_id = this->params_.display_instance_id.c_str();
            const auto display = requested_id[0] == '\0'
                ? manifest->displays.begin()
                : std::find_if(
                    manifest->displays.begin(), manifest->displays.end(),
                    [&](const auto& candidate) {
                        return candidate.instance_id == requested_id;
                    });
            if (display == manifest->displays.end()) {
                set_ui_state(UiState::Failed);
                return;
            }

            const auto backend = static_cast<
                musicrat::backends::LvglDisplayBackend>(this->params_.backend);
            if (!musicrat::backends::lvgl_display_backend_available(backend)) {
                set_ui_state(UiState::Failed);
                return;
            }
            lv_init();
            auto lvgl_display = musicrat::backends::create_lvgl_display({
                .backend = backend,
                .device_path = this->params_.device_path.c_str(),
                .connector_id = this->params_.connector_id,
                .width = this->params_.width,
                .height = this->params_.height,
                .title = this->params_.window_title.c_str(),
            });
            if (lvgl_display.display == nullptr) {
                set_ui_state(UiState::Failed);
                return;
            }

            musicrat::backends::LvglWidgetRenderer renderer;
            renderer.set_interaction_handler(
                handle_interaction, this);
            const auto loaded = renderer.load_surface(
                *display,
                lv_screen_active(),
                lvgl_display.width,
                lvgl_display.height);
            if (loaded.created_count == 0) {
                musicrat::backends::destroy_lvgl_display(
                    lvgl_display, backend);
                set_ui_state(UiState::Failed);
                return;
            }

            set_ui_state(UiState::Running);
            while (!stop_token.stop_requested()) {
                while (drain_one(renderer)) {}
                (void)lv_timer_handler();
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
            musicrat::backends::destroy_lvgl_display(lvgl_display, backend);
            set_ui_state(UiState::Stopped);
        } catch (...) {
            set_ui_state(UiState::Failed);
        }
    }

    void set_ui_state(UiState state) noexcept {
        ui_state_.store(state, std::memory_order_release);
        ui_state_.notify_all();
    }

    static void handle_interaction(
        void* context,
        std::string_view widget_id,
        Messages::ControlEventKind kind,
        double value,
        uint16_t flags) noexcept {
        auto& sink = *static_cast<LvglWidgetSink*>(context);
        sink.queue_interaction(widget_id, kind, value, flags);
    }

    musicrat::backends::LvglWidgetUpdateQueue<> widget_updates_{};
    musicrat::backends::LvglControlEventQueue<> control_events_{};
    std::atomic<uint64_t> blocks_queued_{0};
    std::atomic<uint64_t> blocks_dropped_{0};
    std::atomic<uint64_t> blocks_applied_{0};
    std::atomic<bool> control_overflow_{false};
    uint64_t sequence_number_{0};
    std::atomic<UiState> ui_state_{UiState::Stopped};
    std::jthread ui_thread_{};
};

} // namespace CommRaT