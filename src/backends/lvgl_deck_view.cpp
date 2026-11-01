#include <musicrat/backends/lvgl_deck_view.hpp>
#include <musicrat/backends/media/waveform_beat_grid.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace musicrat::backends {
namespace {
using namespace CommRaT::Messages;

lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, int width) {
    auto* value = lv_label_create(parent);
    lv_label_set_text(value, text);
    lv_obj_set_pos(value, x, y);
    lv_obj_set_width(value, width);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    return value;
}

lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int width) {
    auto* value = lv_button_create(parent);
    lv_obj_set_pos(value, x, y);
    lv_obj_set_size(value, width, 36);
    lv_obj_set_style_bg_color(value, lv_color_hex(0x202830), LV_PART_MAIN);
    lv_obj_set_style_border_color(value, lv_color_hex(0x42484e), LV_PART_MAIN);
    lv_obj_set_style_border_width(value, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(value, 3, LV_PART_MAIN);
    lv_obj_set_style_text_color(value, lv_color_hex(0xf0f0f0), LV_PART_MAIN);
    auto* caption = lv_label_create(value);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    return value;
}

void enabled(lv_obj_t* object, bool value) {
    if (value) lv_obj_remove_state(object, LV_STATE_DISABLED);
    else lv_obj_add_state(object, LV_STATE_DISABLED);
}

DeckMarkerState visible_markers(const media::DeckLoadView& view) {
    const auto& status = view.playback;
    if (!view.connected || view.session_uncertain || status.track_instance_id == 0
        || status.marker_track_instance_id != status.track_instance_id
        || status.duration_frames == 0 || (status.flags & PLAYBACK_STATUS_INVALID) != 0) return {};
    auto markers = status.markers;
    markers.cue_set &= markers.cue_frame < status.duration_frames;
    markers.loop_start_set &= markers.loop_start_frame < status.duration_frames;
    markers.loop_end_set &= markers.loop_end_frame <= status.duration_frames;
    markers.loop_enabled &= markers.loop_start_set && markers.loop_end_set
        && markers.loop_start_frame < markers.loop_end_frame
        && (status.flags & PLAYBACK_STATUS_LOOPING) != 0;
    return markers;
}

const char* load_text(const media::DeckLoadView& view) {
    using State = control::SelectionLoad::State;
    if (view.session_uncertain) return "Player session uncertain; confirm player and restart deck";
    switch (view.load.state()) {
    case State::Idle: return "Browse to select and load a track";
    case State::AwaitingAcceptance: return "Waiting for player acceptance";
    case State::AwaitingCompletion: return view.load.load_state() == MEDIA_LOAD_PREPARED
        ? "Prepared; waiting for audio-boundary commit" : "Preparing selected track";
    case State::Terminal:
        switch (view.load.load_state()) {
        case MEDIA_LOAD_COMMITTED: return "Track committed";
        case MEDIA_LOAD_FAILED: return "Load failed; previous track retained";
        case MEDIA_LOAD_PROTECTED: return "Playing deck protected; pause before loading";
        case MEDIA_LOAD_CANCELLED: return "Load cancelled";
        default: return "Invalid terminal load state";
        }
    case State::Rejected: return "Player rejected load";
    case State::ValidationFailed: return "Selection validation failed";
    case State::InvalidReply: return "Invalid player reply; confirm session and restart deck";
    case State::OutcomeUnknown: return "Load outcome unknown; waiting for matching player status";
    case State::Unavailable: return "Player disconnected; load not sent";
    }
    return "Invalid load state";
}

const char* seek_text(media::DeckSeekState state) {
    using State = media::DeckSeekState;
    switch (state) {
    case State::Idle: return "Paused: tap overview or drag waveform; release to seek";
    case State::AwaitingPosition: return "Seek accepted; waiting for new-generation position";
    case State::PositionObserved: return "Requested position observed in player status";
    case State::Rejected: return "Player rejected seek; retry after checking state";
    case State::Unavailable: return "Seek not sent: player unavailable";
    case State::InvalidTarget: return "Seek not sent: track/state changed or target invalid";
    case State::Failed: return "Seek failed; player reports decode error";
    case State::OutcomeUnknown: return "Seek outcome unknown; no automatic retry";
    }
    return "Invalid seek state";
}
} // namespace

LvglDeckView::LvglDeckView(lv_obj_t* parent, media::DirectoryBrowserWorker& worker,
    LvglDirectoryBrowser::SelectionHandler selection, ControlHandler control, SeekHandler seek, void* context)
    : control_(control), seek_handler_(seek), context_(context) {
    using namespace CommRaT::Parameters;
    const std::array<CompiledPlaybackObservationBinding, 2> times{{
        {.binding_id = 1, .surface_id = "deck", .widget_id = "elapsed",
         .property = WIDGET_PROPERTY_TEXT, .selector = PLAYBACK_OBSERVE_ELAPSED_TIME},
        {.binding_id = 2, .surface_id = "deck", .widget_id = "remaining",
         .property = WIDGET_PROPERTY_TEXT, .selector = PLAYBACK_OBSERVE_REMAINING_TIME},
    }};
    if (time_adapter_.configure(times) != dsp::PlaybackStatusUiAdapterConfigError::None) {
        throw std::logic_error("Invalid built-in deck time bindings");
    }
    object_ = lv_obj_create(parent);
    lv_obj_set_size(object_, 800, 480);
    lv_obj_set_pos(object_, 0, 0);
    lv_obj_set_style_pad_all(object_, 0, 0);
    lv_obj_set_style_border_width(object_, 0, 0);
    lv_obj_set_style_bg_color(object_, lv_color_hex(0x050608), 0);
    lv_obj_set_style_text_color(object_, lv_color_hex(0xf0f0f0), 0);
    active_ = label(object_, "No track committed", 12, 7, 540);
    lv_obj_set_style_text_color(active_, lv_color_hex(0xf4f4f4), 0);
    result_ = label(object_, "Browse to select a track", 12, 29, 540);
    interaction_ = label(object_, "", 12, 47, 776);
    browse_button_ = button(object_, "Browse", 620, 8, 88);
    transport_ = button(object_, LV_SYMBOL_PLAY, 716, 4, 72);
    lv_obj_set_size(transport_, 72, 48);
    lv_obj_set_style_bg_color(transport_, lv_color_hex(0x1d3347), LV_PART_MAIN);
    performance_ = lv_obj_create(object_);
    lv_obj_set_pos(performance_, 0, 64);
    lv_obj_set_size(performance_, 800, 416);
    lv_obj_set_style_pad_all(performance_, 0, 0);
    lv_obj_set_style_bg_color(performance_, lv_color_hex(0x050608), 0);
    lv_obj_set_style_text_color(performance_, lv_color_hex(0xf0f0f0), 0);
    lv_obj_set_style_border_width(performance_, 0, 0);
    waveform_status_ = label(performance_, "", 12, 0, 776);
    waveform_ = lv_obj_create(performance_);
    detail_waveform_ = lv_obj_create(performance_);
    lv_obj_set_pos(detail_waveform_, 12, 16);
    lv_obj_set_size(detail_waveform_, 680, 152);
    lv_obj_set_pos(waveform_, 12, 184);
    lv_obj_set_size(waveform_, 776, 48);
    for (auto* wave : {waveform_, detail_waveform_}) {
        lv_obj_set_style_bg_color(wave, lv_color_hex(0x080c12), 0);
        lv_obj_set_style_border_width(wave, 0, 0);
        lv_obj_set_style_radius(wave, 0, 0);
        lv_obj_set_scrollable(wave, false);
        lv_obj_set_clickable(wave, true);
        lv_obj_set_press_lock(wave, true);
        lv_obj_set_scroll_chain(wave, false);
        lv_obj_add_event_cb(wave, draw_waveform, LV_EVENT_DRAW_MAIN, this);
        lv_obj_add_event_cb(wave, waveform_pointer, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(wave, waveform_pointer, LV_EVENT_PRESSING, this);
        lv_obj_add_event_cb(wave, waveform_pointer, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(wave, waveform_pointer, LV_EVENT_PRESS_LOST, this);
    }
    waveform_legend_ = label(performance_, "", 12, 234, 670);
    time_mode_ = button(performance_, "TIME / ELAPSED", 12, 258, 126);
    lv_obj_set_size(time_mode_, 126, 28);
    timing_ = label(performance_, "--:--.---", 12, 292, 270);
    tempo_panel_ = lv_obj_create(performance_);
    lv_obj_set_pos(tempo_panel_, 304, 258);
    lv_obj_set_size(tempo_panel_, 176, 92);
    lv_obj_set_style_bg_color(tempo_panel_, lv_color_hex(0x101419), 0);
    lv_obj_set_style_border_color(tempo_panel_, lv_color_hex(0x42484e), 0);
    tempo_ = label(tempo_panel_, "TEMPO", 8, 4, 160);
    lv_obj_set_style_text_color(tempo_, lv_color_hex(0x9caec0), 0);
    tempo_value_ = label(tempo_panel_, "+0.00%", 8, 28, 160);
    lv_obj_set_style_text_color(tempo_value_, lv_color_hex(0xf4f4f4), 0);
    bpm_panel_ = lv_obj_create(performance_);
    lv_obj_set_pos(bpm_panel_, 492, 258);
    lv_obj_set_size(bpm_panel_, 190, 92);
    lv_obj_set_style_bg_color(bpm_panel_, lv_color_hex(0x080c12), 0);
    lv_obj_set_style_border_color(bpm_panel_, lv_color_hex(0x9caec0), 0);
    lv_obj_set_style_border_width(bpm_panel_, 2, 0);
    track_bpm_ = label(bpm_panel_, "TRACK --", 8, 6, 174);
    lv_obj_set_style_text_color(track_bpm_, lv_color_hex(0x9caec0), 0);
    auto* bpm_title = label(bpm_panel_, "CURRENT BPM", 8, 27, 174);
    lv_obj_set_style_text_color(bpm_title, lv_color_hex(0x9caec0), 0);
    bpm_value_ = label(bpm_panel_, "--.-", 8, 48, 174);
    lv_obj_set_style_text_color(bpm_value_, lv_color_hex(0xf4f4f4), 0);
#if LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(timing_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_font(tempo_value_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_font(bpm_value_, &lv_font_montserrat_24, 0);
#endif
    cue_button_ = button(performance_, "CUE", 12, 348, 96);
    lv_obj_set_size(cue_button_, 96, 48);
    lv_obj_set_style_bg_color(cue_button_, lv_color_hex(0x30213b), LV_PART_MAIN);
    target_ = label(performance_, "", 120, 354, 562);
    markers_ = label(performance_, "", 12, 376, 776);
    seek_result_ = label(performance_, "", 12, 396, 670);
    pitch_mode_ = label(performance_, "PITCH VARI", 700, 16, 84);
    lv_obj_set_style_text_color(pitch_mode_, lv_color_hex(0x9caec0), 0);
    pitch_value_ = label(performance_, "+0.00%", 700, 34, 84);
    lv_obj_set_style_text_color(pitch_value_, lv_color_hex(0xf4f4f4), 0);
    auto* pitch_maximum = label(performance_, "+16%", 700, 54, 42);
    lv_obj_set_style_text_color(pitch_maximum, lv_color_hex(0x9caec0), 0);
    auto* pitch_center = label(performance_, "0%", 700, 116, 42);
    lv_obj_set_style_text_color(pitch_center, lv_color_hex(0x9caec0), 0);
    pitch_slider_ = lv_slider_create(performance_);
    lv_obj_set_pos(pitch_slider_, 748, 72);
    lv_obj_set_size(pitch_slider_, 22, 100);
    lv_slider_set_orientation(pitch_slider_, LV_SLIDER_ORIENTATION_VERTICAL);
    lv_slider_set_range(pitch_slider_, -1600, 1600);
    lv_slider_set_value(pitch_slider_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(pitch_slider_, lv_color_hex(0x30343a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pitch_slider_, lv_color_hex(0x347bd1), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(pitch_slider_, lv_color_hex(0xf2f2f2), LV_PART_KNOB);
    lv_obj_set_style_pad_all(pitch_slider_, 5, LV_PART_KNOB);
    auto* pitch_minimum = label(performance_, "-16%", 700, 174, 42);
    lv_obj_set_style_text_color(pitch_minimum, lv_color_hex(0x9caec0), 0);
    zoom_out_ = button(detail_waveform_, LV_SYMBOL_MINUS, 618, 3, 26);
    zoom_in_ = button(detail_waveform_, LV_SYMBOL_PLUS, 648, 3, 26);
    lv_obj_set_size(zoom_out_, 26, 24);
    lv_obj_set_size(zoom_in_, 26, 24);
    color_mode_ = button(detail_waveform_, "MONO", 574, 3, 40);
    lv_obj_set_size(color_mode_, 40, 24);
    browser_parent_ = lv_obj_create(object_);
    lv_obj_set_pos(browser_parent_, 0, 64);
    lv_obj_set_size(browser_parent_, 800, 416);
    lv_obj_set_style_pad_all(browser_parent_, 0, 0);
    browser_ = std::make_unique<LvglDirectoryBrowser>(browser_parent_, worker);
    browser_->set_selection_handler(selection, context);
    for (auto* object : {transport_, browse_button_, time_mode_, color_mode_,
                        zoom_in_, zoom_out_}) {
        lv_obj_add_event_cb(object, clicked, LV_EVENT_CLICKED, this);
    }
    for (const auto code : {LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
        lv_obj_add_event_cb(cue_button_, cue_button_event, code, this);
    }
    lv_obj_add_event_cb(pitch_slider_, pitch_changed, LV_EVENT_VALUE_CHANGED, this);
    show_page(false);
    apply({});
}

LvglDeckView::~LvglDeckView() {
    if (cue_button_held_ && control_ != nullptr) {
        static_cast<void>(control_(context_, DECK_CONTROL_CUE_BUTTON_UP, 1.0));
    }
    browser_.reset();
    lv_obj_delete(object_);
}

void LvglDeckView::poll() {
    browser_->poll();
    if (cue_button_held_ && pending_cue_gesture_
        && pending_cue_gesture_->action == CueGesture::ReturnAndPause
        && std::chrono::steady_clock::now() >= cue_hold_deadline_
        && control_(context_, DECK_CONTROL_CUE_BUTTON_HOLD, 1.0)) {
        pending_cue_gesture_->action = CueGesture::Preview;
        lv_label_set_text(interaction_, "CUE preview - release to return");
    }
    resolve_cue_gesture();
    media::WaveformResult result{};
    if (waveform_worker_.poll(result) && result.request == waveform_request_) {
        waveform_result_ = std::move(result);
        update_waveform();
        lv_obj_invalidate(waveform_);
        lv_obj_invalidate(detail_waveform_);
    }
}

void LvglDeckView::update_waveform() {
    media::WaveformRequest request{};
    if (view_.connected && !view_.session_uncertain && view_.playback.track_instance_id != 0
        && !view_.active_path.empty() && view_.active_asset && view_.playback.duration_frames != 0
        && view_.playback.source_sample_rate_hz != 0 && view_.playback.source_channel_count != 0) {
        request = {.track_instance_id = view_.playback.track_instance_id, .path = view_.active_path,
            .frame_count = view_.playback.duration_frames,
            .sample_rate_hz = view_.playback.source_sample_rate_hz,
            .channel_count = view_.playback.source_channel_count, .expected_asset = view_.active_asset};
    }
    if (!(request == waveform_request_)) {
        waveform_request_ = request;
        waveform_worker_.submit(request);
        waveform_result_ = {.request = request, .state = request.track_instance_id != 0
            ? media::WaveformState::Analyzing : media::WaveformState::Unavailable};
        lv_obj_invalidate(waveform_);
        lv_obj_invalidate(detail_waveform_);
    }
    switch (waveform_result_.state) {
    case media::WaveformState::Unavailable:
        lv_label_set_text(waveform_status_, "Waveform unavailable - disconnected or no committed local path");
        break;
    case media::WaveformState::Analyzing:
        lv_label_set_text_fmt(waveform_status_, "Analyzing waveform: %llu / %llu source frames",
            static_cast<unsigned long long>(waveform_result_.analyzed_frames),
            static_cast<unsigned long long>(request.frame_count));
        break;
    case media::WaveformState::Ready:
        lv_label_set_text(waveform_status_, waveform_result_.cache_hit
            ? "Waveform ready (cache) - detail above / whole track below"
            : "Waveform ready - detail above / whole track below");
        break;
    case media::WaveformState::Failed:
        lv_label_set_text_fmt(waveform_status_, "Waveform failed: %s (decoder:%u)",
            media::waveform_error_text(waveform_result_.error),
            static_cast<unsigned>(waveform_result_.decode_error));
        break;
    }
    const auto resolution = waveform_result_.overview && !waveform_result_.overview->detail.empty()
        ? waveform_result_.overview->detail.front().frames_per_bin : 0;
    char grid[64]{"--"};
    const auto& grid_status = view_.playback;
    const bool unreliable_estimate = grid_status.track_instance_id != 0
        && grid_status.beat_grid_track_instance_id == grid_status.track_instance_id
        && (grid_status.beat_grid.flags & BEAT_GRID_FLAG_ANALYZED) != 0
        && (grid_status.beat_grid.flags & BEAT_GRID_FLAG_IMPORTED) == 0;
    if (unreliable_estimate) {
        std::snprintf(grid, sizeof(grid), "estimate unreliable");
    } else if (view_.connected && !view_.session_uncertain
        && media::waveform_grid_available(view_.playback)) {
        const auto beats = media::waveform_beat_lines(view_.playback.beat_grid,
            detail_viewport(), view_.playback.duration_frames,
            static_cast<uint32_t>(std::max(int32_t{2}, lv_obj_get_width(detail_waveform_))));
        if (beats.valid) {
            std::snprintf(grid, sizeof(grid), "%u/%u (%s)", view_.playback.beat_grid.beats_per_bar,
                view_.playback.beat_grid.beat_unit,
                view_.playback.beat_grid.flags & BEAT_GRID_FLAG_IMPORTED ? "Rekordbox" : "configured");
        } else {
            std::snprintf(grid, sizeof(grid), "projection invalid");
        }
    }
    lv_label_set_text_fmt(waveform_legend_, "%us | Grid %s | bin:%llu | RGB:L/M/H | Y:player O:preview",
        detail_seconds_, grid, static_cast<unsigned long long>(resolution));
    enabled(zoom_in_, detail_seconds_ > 2);
    enabled(zoom_out_, detail_seconds_ < 32);
}

media::WaveformViewport LvglDeckView::detail_viewport() const noexcept {
    const double span = static_cast<double>(detail_seconds_) * view_.playback.source_sample_rate_hz;
    const double center = target_selected_ ? static_cast<double>(preview_.media_frame) : view_.playback.media_position_frames;
    return {.start_frame = center - span / 2, .span_frames = span};
}

void LvglDeckView::draw_waveform(lv_event_t* event) {
    const auto& self = *static_cast<LvglDeckView*>(lv_event_get_user_data(event));
    if (self.waveform_result_.state != media::WaveformState::Ready
        || !self.waveform_result_.overview) return;
    const auto& overview = *self.waveform_result_.overview;
    lv_area_t area{};
    auto* object = lv_event_get_target_obj(event);
    const bool detailed = object == self.detail_waveform_;
    lv_obj_get_coords(object, &area);
    const auto width = static_cast<uint32_t>(lv_area_get_width(&area));
    const auto height = lv_area_get_height(&area);
    if (width == 0 || height < 3) return;
    const int32_t ruler_height = detailed ? 20 : 0;
    if (height < ruler_height + 3) return;
    const float center = static_cast<float>(area.y1 + ruler_height) + static_cast<float>(height - ruler_height - 1) / 2;
    const float scale = static_cast<float>(height - ruler_height - 3) / 2;
    auto* layer = lv_event_get_layer(event);
    lv_draw_line_dsc_t line{};
    lv_draw_line_dsc_init(&line);
    line.width = 1;
    const auto viewport = self.detail_viewport().pixel_aligned(width);
    const auto markers = visible_markers(self.view_);
    const bool imported_metadata = self.view_.connected && !self.view_.session_uncertain
        && self.view_.rekordbox_track_instance_id == self.view_.playback.track_instance_id
        && self.view_.playback.track_instance_id != 0;
    const auto& imported_markers = self.view_.rekordbox.markers;
    const auto marker_position = [&](uint64_t frame) {
        return detailed ? viewport.position(static_cast<double>(frame))
            : static_cast<double>(frame) / static_cast<double>(overview.media.frame_count);
    };
    const auto loop_color = markers.loop_enabled ? 0x57db87U : 0xe7b458U;
    const bool loop_region = markers.loop_start_set && markers.loop_end_set
        && markers.loop_start_frame < markers.loop_end_frame;
    if (loop_region) {
        const auto start = marker_position(markers.loop_start_frame);
        const auto end = marker_position(markers.loop_end_frame);
        if (std::isfinite(start) && std::isfinite(end) && end > 0 && start < 1) {
            lv_draw_rect_dsc_t region{};
            lv_draw_rect_dsc_init(&region);
            region.bg_color = lv_color_hex(loop_color);
            region.bg_opa = markers.loop_enabled ? LV_OPA_20 : LV_OPA_10;
            region.border_width = 0;
            const lv_area_t bounds{
                area.x1 + static_cast<int32_t>(std::clamp(start, 0.0, 1.0) * (width - 1)), area.y1 + ruler_height,
                area.x1 + static_cast<int32_t>(std::clamp(end, 0.0, 1.0) * (width - 1)), area.y2};
            lv_draw_rect(layer, &region, &bounds);
        }
    } else if (imported_metadata && imported_markers.loop_start_set && imported_markers.loop_end_set
        && imported_markers.loop_start_frame < imported_markers.loop_end_frame) {
        const auto start = marker_position(imported_markers.loop_start_frame);
        const auto end = marker_position(imported_markers.loop_end_frame);
        if (std::isfinite(start) && std::isfinite(end) && end > 0 && start < 1) {
            lv_draw_rect_dsc_t region{};
            lv_draw_rect_dsc_init(&region);
            region.bg_color = lv_color_hex(0x5be0d0);
            region.bg_opa = LV_OPA_10;
            region.border_width = 0;
            const lv_area_t bounds{
                area.x1 + static_cast<int32_t>(std::clamp(start, 0.0, 1.0) * (width - 1)), area.y1 + ruler_height,
                area.x1 + static_cast<int32_t>(std::clamp(end, 0.0, 1.0) * (width - 1)), area.y2};
            lv_draw_rect(layer, &region, &bounds);
        }
    }
    for (uint32_t x = 0; x < width; ++x) {
        if (detailed) {
            const double start = viewport.start_frame + x * viewport.span_frames / width;
            const double end = viewport.start_frame + (static_cast<double>(x) + 1) * viewport.span_frames / width;
            if (!std::isfinite(start) || end <= 0 || start >= static_cast<double>(overview.media.frame_count)) continue;
        }
        const auto peak = detailed ? overview.detail_column(viewport, x, width) : overview.column(x, width);
        if (self.waveform_mode_ == media::WaveformColorMode::ThreeBand) {
            std::array<float, 4> edges{0, std::clamp(peak.band_rms[0], 0.0F, 1.0F),
                std::clamp(peak.band_rms[1], 0.0F, 1.0F), std::clamp(peak.band_rms[2], 0.0F, 1.0F)};
            std::sort(edges.begin(), edges.end());
            for (std::size_t edge = 1; edge < edges.size(); ++edge) {
                if (edges[edge] - edges[edge - 1] < 0.00001F) continue;
                const auto color = media::waveform_band_color(peak, (edges[edge] + edges[edge - 1]) / 2);
                line.color = lv_color_hex(color);
                for (const float sign : {-1.0F, 1.0F}) {
                    line.p1 = {static_cast<lv_value_precise_t>(area.x1 + static_cast<int32_t>(x)),
                        static_cast<lv_value_precise_t>(center + sign * edges[edge - 1] * scale)};
                    line.p2 = {line.p1.x, static_cast<lv_value_precise_t>(center + sign * edges[edge] * scale)};
                    lv_draw_line(layer, &line);
                }
            }
            continue;
        }
        const auto color = self.waveform_mode_ == media::WaveformColorMode::Rgb
            ? media::waveform_rgb(peak) : 0x42c8eeU;
        line.color = lv_color_hex(self.waveform_mode_ == media::WaveformColorMode::Monochrome
            ? 0x236277 : ((color >> 1) & 0x7f7f7f));
        line.p1 = {static_cast<lv_value_precise_t>(area.x1 + static_cast<int32_t>(x)),
            static_cast<lv_value_precise_t>(center - std::clamp(peak.maximum, -1.0F, 1.0F) * scale)};
        line.p2 = {line.p1.x,
            static_cast<lv_value_precise_t>(center - std::clamp(peak.minimum, -1.0F, 1.0F) * scale)};
        lv_draw_line(layer, &line);
        const auto energy = std::clamp(peak.rms, 0.0F, 1.0F) * scale;
        line.color = lv_color_hex(color);
        line.p1.y = static_cast<lv_value_precise_t>(center - energy);
        line.p2.y = static_cast<lv_value_precise_t>(center + energy);
        lv_draw_line(layer, &line);
    }
    if (self.view_.connected && !self.view_.session_uncertain
        && media::waveform_grid_available(self.view_.playback)) {
        const auto beat_view = detailed ? viewport : media::WaveformViewport{
            .start_frame = 0, .span_frames = static_cast<double>(overview.media.frame_count)};
        const auto beats = media::waveform_beat_lines(self.view_.playback.beat_grid, beat_view,
            overview.media.frame_count, width);
        for (uint32_t tick = 0; tick < beats.count; ++tick) {
            const auto& beat = beats.ticks[tick];
            const auto x = area.x1 + static_cast<int32_t>(std::lround(beat_view.position(beat.frame) * (width - 1)));
            line.color = lv_color_hex(beat.downbeat ? 0xb7c9df : 0x61758e);
            line.p1 = {static_cast<lv_value_precise_t>(x), static_cast<lv_value_precise_t>(area.y1 + ruler_height)};
            line.p2 = {line.p1.x, static_cast<lv_value_precise_t>(area.y2)};
            lv_draw_line(layer, &line);
        }
    }
    if (detailed) {
        const auto ruler = media::waveform_time_ruler(viewport, overview.media.sample_rate_hz, overview.media.frame_count);
        for (uint32_t tick = 0; tick < ruler.count; ++tick) {
            const auto& mark = ruler.ticks[tick];
            const double position = viewport.position(mark.frame);
            const auto x = area.x1 + static_cast<int32_t>(std::lround(position * (width - 1)));
            line.color = lv_color_hex(0x9caec0);
            line.p1 = {static_cast<lv_value_precise_t>(x), static_cast<lv_value_precise_t>(area.y1 + 16)};
            line.p2 = {line.p1.x, static_cast<lv_value_precise_t>(area.y1 + ruler_height)};
            lv_draw_line(layer, &line);
            char text[48]{};
            std::snprintf(text, sizeof(text), "%02llu:%04.1f",
                static_cast<unsigned long long>(mark.seconds / 60), std::fmod(mark.seconds, 60));
            lv_draw_label_dsc_t caption{};
            lv_draw_label_dsc_init(&caption);
            caption.font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
            caption.color = lv_color_hex(0x9caec0);
            caption.text = text;
            caption.text_local = 1;
            const auto left = std::clamp(x + 4, area.x1, std::max(area.x1, area.x2 - 91));
            const lv_area_t text_area{left, area.y1, std::min(area.x2, left + 91), area.y1 + 15};
            lv_draw_label(layer, &caption, &text_area);
        }
    } else if (viewport.span_frames > 0 && std::isfinite(viewport.start_frame)) {
        const double duration = static_cast<double>(overview.media.frame_count);
        const double start = std::clamp(viewport.start_frame, 0.0, duration);
        const double end = std::clamp(viewport.start_frame + viewport.span_frames, 0.0, duration);
        if (duration > 0 && end > start) {
            lv_draw_rect_dsc_t window{};
            lv_draw_rect_dsc_init(&window);
            window.bg_opa = LV_OPA_TRANSP;
            window.border_color = lv_color_hex(0x9caec0);
            window.border_width = 1;
            const lv_area_t bounds{
                area.x1 + static_cast<int32_t>(std::floor(start / duration * (width - 1))), area.y1 + 1,
                area.x1 + static_cast<int32_t>(std::ceil(end / duration * (width - 1))), area.y2 - 1};
            lv_draw_rect(layer, &window, &bounds);
        }
    }
    const auto marker = [&](uint64_t frame, uint32_t color, const char* text) {
        const auto position = marker_position(frame);
        if (!std::isfinite(position) || position < 0 || position > 1) return;
        const auto x = area.x1 + static_cast<int32_t>(std::lround(position * (width - 1)));
        line.color = lv_color_hex(color);
        line.width = 1;
        line.p1 = {static_cast<lv_value_precise_t>(x), static_cast<lv_value_precise_t>(area.y1 + ruler_height)};
        line.p2 = {line.p1.x, static_cast<lv_value_precise_t>(area.y2)};
        lv_draw_line(layer, &line);
        lv_draw_label_dsc_t caption{};
        lv_draw_label_dsc_init(&caption);
        caption.text = text;
        caption.font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
        caption.color = lv_color_hex(color);
        const auto left = std::clamp(x + 2, area.x1, std::max(area.x1, area.x2 - 15));
        const lv_area_t bounds{left, area.y2 - 17, std::min(area.x2, left + 15), area.y2};
        lv_draw_label(layer, &caption, &bounds);
    };
    if (markers.loop_start_set) marker(markers.loop_start_frame, loop_color, "I");
    if (markers.loop_end_set) marker(markers.loop_end_frame, loop_color, "O");
    if (markers.cue_set) marker(markers.cue_frame, 0xe88dfa, "C");
    if (imported_metadata) {
        if (!markers.loop_start_set && imported_markers.loop_start_set) {
            marker(imported_markers.loop_start_frame, 0x5be0d0, "R");
        }
        if (!markers.loop_end_set && imported_markers.loop_end_set) {
            marker(imported_markers.loop_end_frame, 0x5be0d0, "R");
        }
        if (!markers.cue_set && imported_markers.cue_set) {
            marker(imported_markers.cue_frame, 0x5be0d0, "R");
        }
        for (uint32_t cue = 0; cue < self.view_.rekordbox.cue_count; ++cue) {
            const auto& point = self.view_.rekordbox.cues[cue];
            char label[2]{'M', '\0'};
            if (point.hot && point.hot_cue < 8) {
                label[0] = static_cast<char>('A' + point.hot_cue);
            } else if (point.hot) {
                label[0] = '?';
            }
            const auto color = point.loop ? 0x5be0d0U : 0x7cc8ffU;
            marker(point.frame, color, label);
        }
    }
    const auto cursor = [&](double frame, uint32_t color) {
        if (!std::isfinite(frame) || overview.media.frame_count == 0) return;
        const double position = detailed ? self.detail_viewport().position(frame)
            : frame / static_cast<double>(overview.media.frame_count);
        if (position < 0 || position > 1) return;
        const auto x = static_cast<lv_value_precise_t>(area.x1 + position * (width - 1));
        line.color = lv_color_hex(color);
        line.width = 2;
        line.p1 = {x, static_cast<lv_value_precise_t>(area.y1)};
        line.p2 = {x, static_cast<lv_value_precise_t>(area.y2)};
        lv_draw_line(layer, &line);
    };
    cursor(self.view_.playback.media_position_frames, 0xffe26b);
    if (self.target_selected_) cursor(static_cast<double>(self.preview_.media_frame), 0xff8246);
}

void LvglDeckView::show_page(bool browse) {
    cancel_gesture();
    lv_obj_set_hidden(browser_parent_, !browse);
    lv_obj_set_hidden(performance_, browse);
    lv_label_set_text(lv_obj_get_child(browse_button_, 0), browse ? "Deck" : "Browse");
}

bool LvglDeckView::seek_available() const noexcept {
    const auto& status = view_.playback;
    return seek_handler_ != nullptr && view_.connected && !view_.session_uncertain
        && !view_.operation_pending && status.track_instance_id != 0 && status.generation != 0
        && status.duration_frames != 0 && status.source_sample_rate_hz != 0
        && std::isfinite(status.media_position_frames) && status.media_position_frames >= 0
        && (!expected_playing_ || !*expected_playing_)
        && status.transport_sync_mode == TRANSPORT_SYNC_OFF
        && status.decode_error == PLAYBACK_DECODE_ERROR_NONE
        && (status.flags & (PLAYBACK_STATUS_PLAYING | PLAYBACK_STATUS_INVALID | PLAYBACK_STATUS_LOOPING
            | PLAYBACK_STATUS_SYNC_SEEK_PENDING)) == 0
        && !pending_cue_gesture_;
}

void LvglDeckView::update_cue_button() {
    const auto& status = view_.playback;
    constexpr double maximum_exact_frame = 9007199254740991.0;
    const bool stable = control_ != nullptr && view_.connected && !view_.session_uncertain
        && !view_.operation_pending && !pending_cue_gesture_ && !expected_playing_
        && status.track_instance_id != 0 && status.generation != 0
        && status.duration_frames != 0 && status.source_sample_rate_hz != 0
        && status.decode_error == PLAYBACK_DECODE_ERROR_NONE
        && status.transport_sync_mode == TRANSPORT_SYNC_OFF
        && (status.flags & (PLAYBACK_STATUS_INVALID | PLAYBACK_STATUS_LOOPING
            | PLAYBACK_STATUS_SYNC_SEEK_PENDING | PLAYBACK_STATUS_QUANTIZED_ACTION_PENDING)) == 0;
    const bool playing = (status.flags & PLAYBACK_STATUS_PLAYING) != 0;
    const bool position_valid = std::isfinite(status.media_position_frames)
        && status.media_position_frames >= 0
        && status.media_position_frames <= maximum_exact_frame
        && std::round(status.media_position_frames) < static_cast<double>(status.duration_frames);
    const bool cue_set = visible_markers(view_).cue_set;
    enabled(cue_button_, cue_button_held_ || (stable
        && (playing ? cue_set : position_valid)));
}

void LvglDeckView::resolve_cue_gesture() {
    if (!pending_cue_gesture_ || !pending_cue_gesture_->released) return;
    const auto pending = *pending_cue_gesture_;
    const auto& status = view_.playback;
    // Cue returns may reseek the decoder, so same-track generation changes are expected.
    if (!view_.connected || view_.session_uncertain
        || status.track_instance_id != pending.track_instance_id) {
        pending_cue_gesture_.reset();
        lv_label_set_text(interaction_, "CUE outcome unknown; deck or session changed");
        update_cue_button();
        return;
    }

    const auto markers = visible_markers(view_);
    const bool matching_marker_track = status.marker_track_instance_id == pending.track_instance_id;
    bool confirmed = false;
    const char* message = "";
    switch (pending.action) {
    case CueGesture::Set:
        confirmed = matching_marker_track && markers.cue_set && markers.cue_frame == pending.frame;
        message = "CUE point set";
        break;
    case CueGesture::ReturnAndPause:
        confirmed = matching_marker_track && markers.cue_set && markers.cue_frame == pending.frame
            && (status.flags & PLAYBACK_STATUS_PLAYING) == 0
            && std::isfinite(status.media_position_frames)
            && std::abs(status.media_position_frames - static_cast<double>(pending.frame)) <= 1.0;
        message = "Returned to CUE point";
        break;
    case CueGesture::Preview:
        confirmed = pending.playback_observed && matching_marker_track
            && markers.cue_set && markers.cue_frame == pending.frame
            && (status.flags & PLAYBACK_STATUS_PLAYING) == 0
            && std::isfinite(status.media_position_frames)
            && std::abs(status.media_position_frames - static_cast<double>(pending.frame)) <= 1.0;
        message = "CUE preview returned";
        break;
    }
    if (confirmed) {
        pending_cue_gesture_.reset();
        lv_label_set_text(interaction_, message);
    } else if (std::chrono::steady_clock::now() >= cue_gesture_deadline_) {
        pending_cue_gesture_.reset();
        lv_label_set_text(interaction_, "CUE action unconfirmed; outcome or readiness unknown");
    }
    update_cue_button();
}

void LvglDeckView::begin_cue_gesture() {
    const auto& status = view_.playback;
    const auto markers = visible_markers(view_);
    const bool ready = control_ != nullptr && view_.connected && !view_.session_uncertain
        && !view_.operation_pending && !pending_cue_gesture_ && !expected_playing_
        && status.track_instance_id != 0 && status.generation != 0
        && status.duration_frames != 0 && status.source_sample_rate_hz != 0
        && status.decode_error == PLAYBACK_DECODE_ERROR_NONE
        && status.transport_sync_mode == TRANSPORT_SYNC_OFF
        && (status.flags & (PLAYBACK_STATUS_INVALID | PLAYBACK_STATUS_LOOPING | PLAYBACK_STATUS_SYNC_SEEK_PENDING
            | PLAYBACK_STATUS_QUANTIZED_ACTION_PENDING)) == 0;
    const bool playing = (status.flags & PLAYBACK_STATUS_PLAYING) != 0;
    constexpr double maximum_exact_frame = 9007199254740991.0;
    const bool position_valid = std::isfinite(status.media_position_frames)
        && status.media_position_frames >= 0
        && status.media_position_frames <= maximum_exact_frame
        && std::round(status.media_position_frames) < static_cast<double>(status.duration_frames);
    if (!ready || (!playing && !position_valid) || (playing && !markers.cue_set)) {
        lv_label_set_text(interaction_, "CUE unavailable in current player state");
        return;
    }

    const auto frame = playing ? markers.cue_frame
        : static_cast<uint64_t>(std::round(status.media_position_frames));
    const auto action = playing ? CueGesture::ReturnAndPause
        : markers.cue_set && markers.cue_frame == frame ? CueGesture::Preview : CueGesture::Set;
    if (!control_(context_, DECK_CONTROL_CUE_BUTTON_DOWN, 1.0)) {
        lv_label_set_text(interaction_, "CUE press unavailable or queue full");
        return;
    }

    pending_cue_gesture_ = PendingCueGesture{action, status.track_instance_id,
        status.generation, frame, false};
    cue_button_held_ = true;
    cue_hold_deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds{150};
    lv_label_set_text(interaction_, action == CueGesture::Set ? "CUE point queued"
        : action == CueGesture::ReturnAndPause ? "Returning to CUE point"
        : "CUE preview - release to return");
    update_cue_button();
}

void LvglDeckView::end_cue_gesture() {
    if (!cue_button_held_) return;
    cue_button_held_ = false;
    if (!control_(context_, DECK_CONTROL_CUE_BUTTON_UP, 1.0)) {
        pending_cue_gesture_.reset();
        lv_label_set_text(interaction_, "CUE release unavailable; preview outcome unknown");
        update_cue_button();
        return;
    }
    if (pending_cue_gesture_) {
        pending_cue_gesture_->released = true;
        cue_gesture_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    }
    resolve_cue_gesture();
    update_cue_button();
}

void LvglDeckView::update_position_label() {
    if (!target_selected_) {
        lv_label_set_text(target_, seek_available() ? "Tap overview or drag detail while paused"
            : (view_.playback.flags & PLAYBACK_STATUS_PLAYING) ? "Pause to seek" : "Seeking unavailable in current player state");
        return;
    }
    char text[128]{};
    std::snprintf(text, sizeof(text), "Preview: %.3f s - release to seek (audio position unchanged)",
        static_cast<double>(preview_.media_frame) / view_.playback.source_sample_rate_hz);
    lv_label_set_text(target_, text);
}

void LvglDeckView::update_timing() {
    const bool usable = view_.connected && !view_.session_uncertain;
    auto playback = view_.playback;
    if (pending_rate_) {
        if (usable && std::isfinite(playback.playback_rate)
            && std::abs(playback.playback_rate - *pending_rate_) < 0.00005) {
            pending_rate_.reset();
        } else if (std::chrono::steady_clock::now() >= rate_deadline_) {
            pending_rate_.reset();
            lv_label_set_text(interaction_, "Pitch change not confirmed; check player connection");
        }
    }
    if (target_selected_) playback.media_position_frames = static_cast<double>(preview_.media_frame);
    WidgetUpdateBlock times{};
    static_cast<void>(time_adapter_.process(playback, times));
    const auto& time = times.events[remaining_ ? 1 : 0];
    char rate[64]{"--"};
    if (usable && std::isfinite(playback.playback_rate) && playback.playback_rate > 0) {
        std::snprintf(rate, sizeof(rate), "%+.2f%%", (playback.playback_rate - 1) * 100);
    }
    lv_label_set_text_fmt(timing_, "%s%s", target_selected_ ? "PREVIEW " : "",
        usable && time.kind == WIDGET_VALUE_TEXT ? time.text.c_str() : "--:--.---");
    lv_label_set_text_fmt(lv_obj_get_child(time_mode_, 0), "%s / TIME",
        remaining_ ? "REMAIN" : "ELAPSED");
    if (usable && std::isfinite(playback.playback_rate) && playback.playback_rate > 0) {
        const auto slider_rate = pending_rate_.value_or(playback.playback_rate);
        const auto slider_value = static_cast<int32_t>(std::lround((slider_rate - 1.0) * 10000.0));
        if (!lv_obj_has_state(pitch_slider_, LV_STATE_PRESSED)) {
            lv_slider_set_value(pitch_slider_, std::clamp(slider_value, -1600, 1600), LV_ANIM_OFF);
        }
        lv_label_set_text_fmt(pitch_value_, "%+.2f%%%s",
            (slider_rate - 1.0) * 100.0, pending_rate_ ? " *" : "");
    } else {
        if (!lv_obj_has_state(pitch_slider_, LV_STATE_PRESSED)) {
            lv_slider_set_value(pitch_slider_, 0, LV_ANIM_OFF);
        }
        lv_label_set_text(pitch_value_, "--");
    }
    lv_label_set_text(pitch_mode_, usable
        ? playback.pitch_mode == PLAYBACK_PITCH_MODE_LOCKED ? "PITCH MT" : "PITCH VARI"
        : "PITCH --");
    const auto& status = view_.playback;
    const bool bpm_valid = usable && status.track_instance_id != 0
        && status.beat_grid_track_instance_id == status.track_instance_id
        && (status.flags & PLAYBACK_STATUS_INVALID) == 0
        && status.source_sample_rate_hz != 0 && status.duration_frames != 0
        && std::isfinite(status.track_bpm) && status.track_bpm > 0
        && std::isfinite(status.current_bpm) && status.current_bpm > 0
        && std::isfinite(status.playback_rate) && status.playback_rate > 0;
    if (bpm_valid) {
        const bool unreliable_estimate = (status.beat_grid.flags & BEAT_GRID_FLAG_ANALYZED) != 0
            && (status.beat_grid.flags & BEAT_GRID_FLAG_IMPORTED) == 0;
        char track_bpm[64]{};
        char current_bpm[32]{};
        lv_label_set_text_fmt(tempo_value_, "%s%s", rate,
            pending_rate_ ? " *" : "");
        std::snprintf(track_bpm, sizeof(track_bpm), unreliable_estimate
            ? "TRACK BPM %.1f (estimate unreliable)" : "TRACK BPM %.1f", status.track_bpm);
        std::snprintf(current_bpm, sizeof(current_bpm), "%.1f", status.current_bpm);
        lv_label_set_text(track_bpm_, track_bpm);
        lv_label_set_text(bpm_value_, current_bpm);
    } else {
        lv_label_set_text_fmt(tempo_value_, "%s", usable ? rate : "--");
        lv_label_set_text(track_bpm_, "TRACK BPM --");
        lv_label_set_text(bpm_value_, "--.-");
    }
}

void LvglDeckView::update_markers() {
    const auto markers = visible_markers(view_);
    const auto rate = view_.playback.source_sample_rate_hz;
    const auto format = [rate](char* text, std::size_t size, bool set, uint64_t frame) {
        if (set && rate != 0) std::snprintf(text, size, "%.3fs", static_cast<double>(frame) / rate);
        else std::snprintf(text, size, "--");
    };
    char cue[48]{}, start[48]{}, end[48]{};
    format(cue, sizeof(cue), markers.cue_set, markers.cue_frame);
    format(start, sizeof(start), markers.loop_start_set, markers.loop_start_frame);
    format(end, sizeof(end), markers.loop_end_set, markers.loop_end_frame);
    const bool range = markers.loop_start_set && markers.loop_end_set
        && markers.loop_start_frame < markers.loop_end_frame;
    const bool imported = view_.connected && !view_.session_uncertain
        && view_.rekordbox_track_instance_id == view_.playback.track_instance_id
        && view_.playback.track_instance_id != 0;
    char rekordbox[80]{"--"};
    if (imported && view_.rekordbox.cue_count != 0 && rate != 0) {
        const auto& first = view_.rekordbox.cues[0];
        const char cue_id = first.hot
            ? first.hot_cue < 8 ? static_cast<char>('A' + first.hot_cue) : '?'
            : 'M';
        std::snprintf(rekordbox, sizeof(rekordbox), "%c %.1fs +%u cues (display only)",
            cue_id, static_cast<double>(first.frame) / rate, view_.rekordbox.cue_count - 1);
    } else if (imported && view_.rekordbox.grid_set) {
        std::snprintf(rekordbox, sizeof(rekordbox), "grid imported");
    } else if (imported) {
        switch (view_.rekordbox_import_error) {
        case media::RekordboxImportError::InvalidRequest:
            std::snprintf(rekordbox, sizeof(rekordbox), "path outside media root");
            break;
        case media::RekordboxImportError::Io:
            std::snprintf(rekordbox, sizeof(rekordbox), "read failed");
            break;
        case media::RekordboxImportError::Malformed:
            std::snprintf(rekordbox, sizeof(rekordbox), "malformed ANLZ");
            break;
        case media::RekordboxImportError::ResourceLimit:
            std::snprintf(rekordbox, sizeof(rekordbox), "scan limit reached");
            break;
        case media::RekordboxImportError::None:
        case media::RekordboxImportError::NotFound:
            std::snprintf(rekordbox, sizeof(rekordbox), "not found");
            break;
        }
    }
    lv_label_set_text_fmt(markers_, "Cue %s | In %s Out %s | Loop %s | RB %s", cue, start, end,
        markers.loop_enabled ? "active" : range ? "stored (readiness unknown)"
        : markers.loop_start_set || markers.loop_end_set ? "incomplete / invalid" : "--",
        rekordbox);
}

void LvglDeckView::cancel_gesture() {
    gesture_surface_ = nullptr;
    target_selected_ = false;
    lv_obj_invalidate(waveform_);
    lv_obj_invalidate(detail_waveform_);
    update_position_label();
    update_timing();
}

void LvglDeckView::apply(const media::DeckLoadView& view) {
    const bool had_preview = target_selected_;
    if (view.playback.track_instance_id != view_.playback.track_instance_id
        || !view.connected || view.session_uncertain) {
        pending_rate_.reset();
    }
    if (view.playback.media_position_frames != view_.playback.media_position_frames
        || view.playback.generation != view_.playback.generation
        || view.playback.marker_track_instance_id != view_.playback.marker_track_instance_id
        || view.playback.markers != view_.playback.markers
        || view.playback.flags != view_.playback.flags) {
        lv_obj_invalidate(waveform_);
        lv_obj_invalidate(detail_waveform_);
    }
    if (view.connected != view_.connected || view.session_uncertain != view_.session_uncertain
        || view.playback.beat_grid_track_instance_id != view_.playback.beat_grid_track_instance_id
        || view.playback.beat_grid != view_.playback.beat_grid
        || view.rekordbox_track_instance_id != view_.rekordbox_track_instance_id
        || view.rekordbox != view_.rekordbox) {
        lv_obj_invalidate(waveform_);
        lv_obj_invalidate(detail_waveform_);
    }
    if (view.playback.track_instance_id != view_.playback.track_instance_id
        || view.playback.generation != view_.playback.generation
        || view.playback.duration_frames != view_.playback.duration_frames
        || view.playback.source_sample_rate_hz != view_.playback.source_sample_rate_hz) {
        target_selected_ = false;
        gesture_surface_ = nullptr;
    }
    view_ = view;
    if (pending_cue_gesture_
        && (view_.playback.flags & PLAYBACK_STATUS_PLAYING) != 0) {
        pending_cue_gesture_->playback_observed = true;
    }
    resolve_cue_gesture();
    if (!seek_available()) {
        target_selected_ = false;
        gesture_surface_ = nullptr;
    }
    if (had_preview != target_selected_) {
        lv_obj_invalidate(waveform_);
        lv_obj_invalidate(detail_waveform_);
    }
    const bool usable = view.connected && !view.session_uncertain;
    const bool transport = usable && !view.operation_pending && !pending_cue_gesture_
        && view.playback.track_instance_id != 0;
    enabled(transport_, transport && control_ != nullptr);
    enabled(pitch_slider_, transport && control_ != nullptr
        && std::isfinite(view.playback.playback_rate) && view.playback.playback_rate > 0);
    update_cue_button();
    lv_label_set_text(active_, view.active_name.empty()
        ? (view.playback.track_instance_id == 0 ? "No track committed" : "Track loaded externally")
        : view.active_name.c_str());
    update_timing();
    update_markers();
    lv_label_set_text(result_, load_text(view));
    update_position_label();
    update_waveform();
    lv_label_set_text_fmt(seek_result_, "%s", seek_text(view.seek_state));
    lv_label_set_text(lv_obj_get_child(transport_, 0),
        expected_playing_.value_or((view.playback.flags & PLAYBACK_STATUS_PLAYING) != 0)
            ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (expected_playing_) {
        if (usable && *expected_playing_ == ((view.playback.flags & PLAYBACK_STATUS_PLAYING) != 0)) {
            lv_label_set_text(interaction_, *expected_playing_ ? "Playing confirmed" : "Paused confirmed");
            expected_playing_.reset();
        } else if (std::chrono::steady_clock::now() >= control_deadline_) {
            lv_label_set_text(interaction_, "Transport not confirmed; check player connection");
            expected_playing_.reset();
        }
    }
}

void LvglDeckView::waveform_pointer(lv_event_t* event) {
    auto& self = *static_cast<LvglDeckView*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESS_LOST) {
        self.cancel_gesture();
        return;
    }
    auto* surface = lv_event_get_target_obj(event);
    auto* input = lv_event_get_indev(event);
    if (!input) return;
    if (!self.seek_available()) {
        if (code == LV_EVENT_PRESSED) lv_label_set_text(self.interaction_, "Pause to seek; loops/sync/pending operations must be off");
        self.cancel_gesture();
        return;
    }
    lv_point_t point{};
    lv_indev_get_point(input, &point);
    lv_area_t area{};
    lv_obj_get_coords(surface, &area);
    if (point.x < area.x1 || point.x > area.x2 || point.y < area.y1 || point.y > area.y2) {
        self.cancel_gesture();
        return;
    }
    const auto& status = self.view_.playback;
    const bool detail = surface == self.detail_waveform_;
    const double width = lv_area_get_width(&area) - 1;
    if (width <= 0) return;
    if (code == LV_EVENT_PRESSED) {
        self.gesture_surface_ = surface;
        self.gesture_x_ = point.x;
        self.gesture_frame_ = status.media_position_frames;
        self.gesture_span_ = self.detail_viewport().span_frames;
        self.gesture_dragged_ = false;
        const double clicked_frame = (point.x - area.x1) / width * static_cast<double>(status.duration_frames);
        const auto viewport = self.detail_viewport();
        self.gesture_outline_ = !detail && clicked_frame >= viewport.start_frame
            && clicked_frame <= viewport.start_frame + viewport.span_frames;
    } else if (self.gesture_surface_ != surface) return;
    const auto delta = point.x - self.gesture_x_;
    self.gesture_dragged_ |= std::abs(delta) >= 4;
    double frame = self.gesture_frame_;
    if (detail) frame -= delta * self.gesture_span_ / width;
    else if (self.gesture_outline_ && self.gesture_dragged_) {
        frame += delta / width * static_cast<double>(status.duration_frames);
    } else frame = (point.x - area.x1) / width * static_cast<double>(status.duration_frames);
    const auto last = status.duration_frames - 1;
    const auto target = frame <= 0 ? uint64_t{0} : frame >= static_cast<double>(last)
        ? last : static_cast<uint64_t>(std::round(frame));
    self.preview_ = {.track_instance_id = status.track_instance_id, .generation = status.generation,
        .media_frame = target};
    self.target_selected_ = true;
    self.update_position_label();
    self.update_timing();
    lv_obj_invalidate(self.waveform_);
    lv_obj_invalidate(self.detail_waveform_);
    if (code != LV_EVENT_RELEASED) return;
    const bool commit = !detail || self.gesture_dragged_;
    const auto request = self.preview_;
    self.cancel_gesture();
    if (!commit) return;
    const bool sent = self.seek_handler_(self.context_, request);
    lv_label_set_text(self.interaction_, sent ? "Seek queued; waiting for player status"
        : "Seek unavailable or worker busy; retry");
    if (sent) {
        self.view_.operation_pending = true;
        self.apply(self.view_);
    }
}

void LvglDeckView::clicked(lv_event_t* event) {
    auto& self = *static_cast<LvglDeckView*>(lv_event_get_user_data(event));
    auto* target = lv_event_get_target_obj(event);
    if (lv_obj_has_state(target, LV_STATE_DISABLED)) return;
    if (target == self.browse_button_) {
        const bool showing_browser = !lv_obj_is_hidden(self.browser_parent_);
        self.show_page(!showing_browser);
    }
    else if (target == self.time_mode_) {
        self.remaining_ = !self.remaining_;
        self.apply(self.view_);
    } else if (target == self.color_mode_ || target == self.zoom_in_ || target == self.zoom_out_) {
        self.cancel_gesture();
        if (target == self.color_mode_) {
            self.waveform_mode_ = self.waveform_mode_ == media::WaveformColorMode::Monochrome
                ? media::WaveformColorMode::ThreeBand : self.waveform_mode_ == media::WaveformColorMode::ThreeBand
                ? media::WaveformColorMode::Rgb : media::WaveformColorMode::Monochrome;
            lv_label_set_text(lv_obj_get_child(self.color_mode_, 0),
                self.waveform_mode_ == media::WaveformColorMode::Monochrome ? "MONO"
                : self.waveform_mode_ == media::WaveformColorMode::ThreeBand ? "3B" : "RGB");
        } else if (target == self.zoom_in_) self.detail_seconds_ /= 2;
        else self.detail_seconds_ *= 2;
        self.update_waveform();
        lv_obj_invalidate(self.waveform_);
        lv_obj_invalidate(self.detail_waveform_);
    } else if (target == self.transport_ && self.control_ != nullptr) {
        self.cancel_gesture();
        const bool playing = self.expected_playing_.value_or(
            (self.view_.playback.flags & PLAYBACK_STATUS_PLAYING) != 0);
        const auto type = playing ? DECK_CONTROL_PAUSE : DECK_CONTROL_PLAY;
        if (self.control_(self.context_, type, 1.0)) {
            self.expected_playing_ = type == DECK_CONTROL_PLAY;
            lv_label_set_text(lv_obj_get_child(self.transport_, 0),
                *self.expected_playing_ ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
            self.control_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            lv_label_set_text(self.interaction_, "Waiting for transport confirmation");
            self.update_cue_button();
        } else lv_label_set_text(self.interaction_, "Transport unavailable or queue full; retry");
    }
}

void LvglDeckView::cue_button_event(lv_event_t* event) {
    auto& self = *static_cast<LvglDeckView*>(lv_event_get_user_data(event));
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        if (!lv_obj_has_state(self.cue_button_, LV_STATE_DISABLED)) self.begin_cue_gesture();
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        self.end_cue_gesture();
        break;
    default:
        break;
    }
}

void LvglDeckView::pitch_changed(lv_event_t* event) {
    auto& self = *static_cast<LvglDeckView*>(lv_event_get_user_data(event));
    if (lv_obj_has_state(self.pitch_slider_, LV_STATE_DISABLED) || self.control_ == nullptr) return;
    const auto value = lv_slider_get_value(self.pitch_slider_);
    const double rate = 1.0 + static_cast<double>(value) / 10000.0;
    if (self.control_(self.context_, DECK_CONTROL_SET_RATE, rate)) {
        self.pending_rate_ = rate;
        self.rate_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        lv_label_set_text(self.interaction_, "Pitch fader queued; waiting for player status");
        self.update_timing();
    } else {
        lv_label_set_text(self.interaction_, "Pitch unavailable or control queue full; retry");
    }
}
} // namespace musicrat::backends
