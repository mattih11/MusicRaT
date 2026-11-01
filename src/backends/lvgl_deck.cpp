#include <musicrat/modules/lvgl_deck.hpp>
#include <musicrat/backends/lvgl_deck_view.hpp>

#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string_view>

namespace CommRaT {

musicrat::launcher::DescriptorMetadata LvglDeck::descriptor_metadata() {
    using namespace musicrat::launcher;
    DescriptorMetadata metadata{};
    metadata.musicrat_ports.ports = {
        {.id = "deck_controls", .display_name = "Deck Controls",
         .direction = PORT_DIRECTION_OUTPUT, .port_index = 0, .domain = PORT_DOMAIN_CONTROL},
        {.id = "playback_status", .display_name = "Playback Status",
         .direction = PORT_DIRECTION_SYNCED_INPUT, .port_index = 0, .domain = PORT_DOMAIN_TELEMETRY},
        {.id = "player", .display_name = "Player Commands",
         .direction = PORT_DIRECTION_REMOTE, .port_index = 0, .domain = PORT_DOMAIN_COMMAND},
    };
    metadata.musicrat_parameters.parameters = {
        {.id = Parameters::LVGL_DECK_ROOT_PARAMETER_ID, .name = "media_root",
         .display_name = "Media Root", .group = "Library", .kind = PARAMETER_KIND_TEXT,
         .unit = "", .choices = {}},
        {.id = Parameters::LVGL_DECK_BACKEND_PARAMETER_ID, .name = "backend",
         .display_name = "Display Backend", .group = "Display", .kind = PARAMETER_KIND_CHOICE,
         .unit = "", .choices = {{.value = 0, .label = "Headless"}, {.value = 3, .label = "SDL Window"}}},
        {.id = Parameters::LVGL_DECK_TIMEOUT_PARAMETER_ID, .name = "load_timeout_ms",
         .display_name = "Load Result Timeout", .group = "Load", .kind = PARAMETER_KIND_INTEGER,
         .unit = "ms", .minimum = 1, .maximum = 600000, .step = 1, .choices = {}},
    };
    return metadata;
}

LvglDeck::~LvglDeck() {
    this->stop();
    loads_.reset();
}

commrat::LifecycleResult LvglDeck::on_enable() {
    if (params_.media_root.empty() || params_.load_timeout_ms == 0
        || params_.load_timeout_ms > 600000 || (params_.backend != 0 && params_.backend != 3)) {
        std::fprintf(stderr, "LVGL deck: invalid root/backend/load timeout\n");
        return commrat::LifecycleResult::Failed;
    }
    if (loads_ && params_.load_timeout_ms != ui_params_.load_timeout_ms) {
        std::fprintf(stderr, "LVGL deck: changing load timeout requires a new module instance\n");
        return commrat::LifecycleResult::Failed;
    }
    ui_params_ = params_;
    // Worker remains alive until stop/destruction: process() may still run during disable.
    if (!loads_) loads_ = std::make_unique<musicrat::backends::media::DeckLoadWorker>(
        this, send_load, params_.load_timeout_ms, send_seek,
        std::filesystem::path{params_.media_root.c_str()});
    ui_state_.store(UiState::Starting, std::memory_order_release);
    ui_thread_ = std::jthread([this](std::stop_token stop) { ui_loop(stop); });
    auto state = ui_state_.load(std::memory_order_acquire);
    while (state == UiState::Starting) {
        ui_state_.wait(state, std::memory_order_acquire);
        state = ui_state_.load(std::memory_order_acquire);
    }
    if (state == UiState::Running) return commrat::LifecycleResult::Success;
    ui_thread_.join();
    return commrat::LifecycleResult::Failed;
}

void LvglDeck::on_disable() {
    if (ui_thread_.joinable()) {
        ui_thread_.request_stop();
        ui_thread_.join();
    }
    waveform_state_.store(musicrat::backends::media::WaveformState::Unavailable, std::memory_order_release);
    Messages::DeckControlEvent discarded{};
    while (controls_.pop(discarded)) {
        if (discarded.type == Messages::DECK_CONTROL_CUE_BUTTON_UP) {
            cue_release_pending_.store(true, std::memory_order_release);
        }
    }
}

void LvglDeck::process(const commrat::Synced<Messages::PlaybackStatusBlock>& status,
                      Messages::DeckControlEventBlock& output) {
    if (loads_ && status.is_fresh()) loads_->publish(status.value());
    output.events.clear();
    output.timestamp_ns = commrat::Time::now();
    output.sequence_number = sequence_++;
    Messages::DeckControlEvent event{};
    while (output.events.size() < Messages::DeckControlEventBlock::MAX_EVENTS && controls_.pop(event)) {
        output.events.push_back(event);
    }
    if (output.events.size() < Messages::DeckControlEventBlock::MAX_EVENTS
        && cue_release_pending_.exchange(false, std::memory_order_acq_rel)) {
        output.events.push_back({.type = Messages::DECK_CONTROL_CUE_BUTTON_UP});
    }
    if (rate_update_pending_.exchange(false, std::memory_order_acq_rel)) {
        if (output.events.size() < Messages::DeckControlEventBlock::MAX_EVENTS) {
            output.events.push_back({.type = Messages::DECK_CONTROL_SET_RATE,
                .value = std::bit_cast<double>(rate_value_bits_.load(std::memory_order_acquire))});
        } else {
            rate_update_pending_.store(true, std::memory_order_release);
        }
    }
}

bool LvglDeck::queue_selection(const Messages::MediaSelectionResult& selection) noexcept {
    return loads_ && loads_->submit(selection);
}

bool LvglDeck::queue_control(Messages::DeckControlType type) noexcept {
    return queue_control(type, 1.0);
}

bool LvglDeck::queue_control(Messages::DeckControlType type, double value) noexcept {
    const bool cue_control = type == Messages::DECK_CONTROL_SET_CUE
        || type == Messages::DECK_CONTROL_RETURN_TO_CUE
        || type == Messages::DECK_CONTROL_CLEAR_CUE
        || type == Messages::DECK_CONTROL_CUE_BUTTON_DOWN
        || type == Messages::DECK_CONTROL_CUE_BUTTON_HOLD
        || type == Messages::DECK_CONTROL_CUE_BUTTON_UP;
    constexpr double maximum_exact_frame = 9007199254740991.0;
    if ((type != Messages::DECK_CONTROL_PLAY && type != Messages::DECK_CONTROL_PAUSE
            && type != Messages::DECK_CONTROL_SET_RATE && !cue_control)
        || !std::isfinite(value)
        || (type == Messages::DECK_CONTROL_SET_CUE
            && (value < 0.0 || value > maximum_exact_frame || std::trunc(value) != value))
        || (type == Messages::DECK_CONTROL_SET_RATE && (value < 0.25 || value > 4.0))) return false;
    if (!connected() || active_track() == 0
        || (type != Messages::DECK_CONTROL_CUE_BUTTON_UP
            && operation_pending_.load(std::memory_order_acquire))) return false;
    if (type == Messages::DECK_CONTROL_CUE_BUTTON_DOWN
        && cue_release_pending_.load(std::memory_order_acquire)) return false;
    if (type == Messages::DECK_CONTROL_SET_RATE) {
        rate_value_bits_.store(std::bit_cast<uint64_t>(value), std::memory_order_relaxed);
        rate_update_pending_.store(true, std::memory_order_release);
        return true;
    }
    if (controls_.push({.type = type, .value = value})) return true;
    if (type == Messages::DECK_CONTROL_CUE_BUTTON_UP) {
        bool expected = false;
        return cue_release_pending_.compare_exchange_strong(expected, true,
            std::memory_order_release, std::memory_order_relaxed);
    }
    return false;
}

bool LvglDeck::queue_seek(const musicrat::backends::media::DeckSeekRequest& request) noexcept {
    return loads_ && loads_->submit_seek(request);
}

std::optional<Messages::SeekMedia::Reply> LvglDeck::send_seek(void* context, const Messages::SeekMedia& command) {
    auto& deck = *static_cast<LvglDeck*>(context);
    const auto reply = deck.template remote<Messages::AudioBlock>()
        .template send_command<Messages::SeekMedia>(command, commrat::Milliseconds(500));
    if (!reply) return std::nullopt;
    return reply->payload;
}

std::optional<Messages::LoadMedia::Reply> LvglDeck::send_load(void* context, const Messages::LoadMedia& command) {
    auto& deck = *static_cast<LvglDeck*>(context);
    const auto reply = deck.template remote<Messages::AudioBlock>()
        .template send_command<Messages::LoadMedia>(command, commrat::Milliseconds(500));
    if (!reply) return std::nullopt;
    return reply->payload;
}

bool LvglDeck::selected(void* context, const Messages::MediaSelectionResult& selection) noexcept {
    return static_cast<LvglDeck*>(context)->queue_selection(selection);
}

bool LvglDeck::control(void* context, Messages::DeckControlType type, double value) noexcept {
    return static_cast<LvglDeck*>(context)->queue_control(type, value);
}

bool LvglDeck::seek(void* context, const musicrat::backends::media::DeckSeekRequest& request) noexcept {
    return static_cast<LvglDeck*>(context)->queue_seek(request);
}

void LvglDeck::set_ui_state(UiState state) noexcept {
    ui_state_.store(state, std::memory_order_release);
    ui_state_.notify_all();
}

void LvglDeck::ui_loop(std::stop_token stop) {
    using namespace musicrat::backends;
    const auto backend = static_cast<LvglDisplayBackend>(ui_params_.backend);
    LvglDisplayHandle display{};
    try {
        if (!lvgl_display_backend_available(backend)) {
            std::fprintf(stderr, "LVGL deck: requested backend is not available\n");
            set_ui_state(UiState::Failed);
            return;
        }
        lv_init();
        display = create_lvgl_display({
            .backend = backend, .device_path = "", .input_device_path = "",
            .width = 800, .height = 480, .title = "MusicRaT Deck - workflow prototype",
        });
        if (display.display == nullptr) {
            std::fprintf(stderr, "LVGL deck: display creation failed\n");
            set_ui_state(UiState::Failed);
            return;
        }
        {
            media::DirectoryBrowserWorker browser_worker{ui_params_.media_root.c_str()};
            LvglDeckView presentation{lv_display_get_screen_active(display.display),
                browser_worker, selected, control, seek, this};
            media::DeckLoadView view{};
            auto previous = std::chrono::steady_clock::now();
            set_ui_state(UiState::Running);
            while (!stop.stop_requested()) {
                const auto now = std::chrono::steady_clock::now();
                const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(now - previous);
                lv_tick_inc(static_cast<uint32_t>(ticks.count()));
                previous += ticks;
                presentation.poll();
                while (loads_->poll(view)) {}
                const bool usable = view.connected && !view.session_uncertain;
                connected_.store(usable, std::memory_order_release);
                load_result_.store(view.load.load_state(), std::memory_order_release);
                active_track_.store(view.playback.track_instance_id, std::memory_order_release);
                load_id_.store(view.load.player_request_id(), std::memory_order_release);
                playback_flags_.store(view.playback.flags, std::memory_order_release);
                load_state_.store(view.load.state(), std::memory_order_release);
                operation_pending_.store(view.operation_pending, std::memory_order_release);
                generation_.store(view.playback.generation, std::memory_order_release);
                const double position = view.playback.media_position_frames;
                media_frame_.store(std::isfinite(position) && position >= 0.0
                    && static_cast<long double>(position) < static_cast<long double>(UINT64_MAX)
                    ? static_cast<uint64_t>(position) : 0, std::memory_order_release);
                seek_state_.store(view.seek_state, std::memory_order_release);
                presentation.apply(view);
                waveform_state_.store(presentation.waveform_state(), std::memory_order_release);
                static_cast<void>(lv_timer_handler());
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
        }
        destroy_lvgl_display(display, backend);
        connected_.store(false, std::memory_order_release);
        set_ui_state(UiState::Stopped);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "LVGL deck: %s\n", error.what());
        if (display.display != nullptr) destroy_lvgl_display(display, backend);
        connected_.store(false, std::memory_order_release);
        waveform_state_.store(media::WaveformState::Unavailable, std::memory_order_release);
        set_ui_state(UiState::Failed);
    }
}

} // namespace CommRaT
