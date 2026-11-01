#pragma once

#include <musicrat/backends/lvgl_directory_browser.hpp>
#include <musicrat/backends/media/deck_load_worker.hpp>
#include <musicrat/backends/media/waveform_overview.hpp>
#include <musicrat/dsp/playback_status_ui_adapter.hpp>
#include <musicrat/protocol/deck_control.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace musicrat::backends {

class LvglDeckView {
public:
    using ControlHandler = bool (*)(void*, CommRaT::Messages::DeckControlType, double) noexcept;
    using SeekHandler = bool (*)(void*, const media::DeckSeekRequest&) noexcept;

    LvglDeckView(lv_obj_t* parent, media::DirectoryBrowserWorker& worker,
                 LvglDirectoryBrowser::SelectionHandler selection,
                 ControlHandler control, SeekHandler seek, void* context);
    ~LvglDeckView();
    void poll();
    void apply(const media::DeckLoadView& view);
    [[nodiscard]] lv_obj_t* object() const noexcept { return object_; }
    [[nodiscard]] media::WaveformState waveform_state() const noexcept { return waveform_result_.state; }
    [[nodiscard]] media::WaveformColorMode waveform_mode() const noexcept { return waveform_mode_; }
    [[nodiscard]] uint32_t detail_seconds() const noexcept { return detail_seconds_; }
    [[nodiscard]] media::WaveformViewport detail_viewport() const noexcept;

private:
    enum class CueGesture : uint8_t { Set, ReturnAndPause, Preview };
    struct PendingCueGesture {
        CueGesture action;
        uint64_t track_instance_id;
        uint64_t generation;
        uint64_t frame;
        bool released{false};
        bool playback_observed{false};
    };

    static void clicked(lv_event_t* event);
    static void cue_button_event(lv_event_t* event);
    static void pitch_changed(lv_event_t* event);
    static void waveform_pointer(lv_event_t* event);
    static void draw_waveform(lv_event_t* event);
    void update_waveform();
    void show_page(bool browse);
    void update_position_label();
    void update_timing();
    void update_markers();
    void update_cue_button();
    void resolve_cue_gesture();
    void begin_cue_gesture();
    void end_cue_gesture();
    void cancel_gesture();
    [[nodiscard]] bool seek_available() const noexcept;

    lv_obj_t* object_{nullptr};
    lv_obj_t* active_{nullptr};
    lv_obj_t* timing_{nullptr};
    lv_obj_t* result_{nullptr};
    lv_obj_t* interaction_{nullptr};
    lv_obj_t* transport_{nullptr};
    lv_obj_t* browse_button_{nullptr};
    lv_obj_t* time_mode_{nullptr};
    lv_obj_t* performance_{nullptr};
    lv_obj_t* browser_parent_{nullptr};
    lv_obj_t* target_{nullptr};
    lv_obj_t* tempo_{nullptr};
    lv_obj_t* tempo_panel_{nullptr};
    lv_obj_t* tempo_value_{nullptr};
    lv_obj_t* bpm_panel_{nullptr};
    lv_obj_t* bpm_value_{nullptr};
    lv_obj_t* track_bpm_{nullptr};
    lv_obj_t* markers_{nullptr};
    lv_obj_t* seek_result_{nullptr};
    lv_obj_t* waveform_{nullptr};
    lv_obj_t* waveform_status_{nullptr};
    lv_obj_t* detail_waveform_{nullptr};
    lv_obj_t* waveform_legend_{nullptr};
    lv_obj_t* color_mode_{nullptr};
    lv_obj_t* zoom_in_{nullptr};
    lv_obj_t* zoom_out_{nullptr};
    lv_obj_t* pitch_slider_{nullptr};
    lv_obj_t* pitch_value_{nullptr};
    lv_obj_t* pitch_mode_{nullptr};
    lv_obj_t* cue_button_{nullptr};
    media::WaveformColorMode waveform_mode_{media::WaveformColorMode::Monochrome};
    uint32_t detail_seconds_{8};
    media::WaveformWorker waveform_worker_;
    media::WaveformRequest waveform_request_{};
    media::WaveformResult waveform_result_{};
    std::unique_ptr<LvglDirectoryBrowser> browser_;
    media::DeckLoadView view_{};
    media::DeckSeekRequest preview_{};
    bool target_selected_{false};
    lv_obj_t* gesture_surface_{nullptr};
    int32_t gesture_x_{0};
    double gesture_frame_{0};
    double gesture_span_{0};
    bool gesture_dragged_{false};
    bool gesture_outline_{false};
    bool remaining_{false};
    ControlHandler control_;
    SeekHandler seek_handler_;
    void* context_;
    musicrat::dsp::PlaybackStatusUiAdapter time_adapter_{};
    std::optional<bool> expected_playing_{};
    std::chrono::steady_clock::time_point control_deadline_{};
    std::optional<double> pending_rate_{};
    std::chrono::steady_clock::time_point rate_deadline_{};
    std::optional<PendingCueGesture> pending_cue_gesture_{};
    bool cue_button_held_{false};
    std::chrono::steady_clock::time_point cue_hold_deadline_{};
    std::chrono::steady_clock::time_point cue_gesture_deadline_{};
};

} // namespace musicrat::backends
