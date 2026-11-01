#include <musicrat/backends/lvgl_deck_view.hpp>
#include <musicrat/backends/lvgl_display_backend.hpp>
#include <musicrat/backends/audio/wav_writer.hpp>

#include "../support/directory_fixture.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <numbers>
#include <limits>
#include <string_view>
#include <thread>
#include <vector>
#include <algorithm>

namespace {
using namespace CommRaT::Messages;
using musicrat::backends::LvglDeckView;

lv_obj_t* find_button(lv_obj_t* object, std::string_view text) {
    if (lv_obj_check_type(object, &lv_button_class)) {
        auto* caption = lv_obj_get_child(object, 0);
        if (caption && std::string_view{lv_label_get_text(caption)} == text) return object;
    }
    for (uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
        if (auto* found = find_button(lv_obj_get_child(object, index), text)) return found;
    }
    return nullptr;
}

lv_obj_t* find_slider(lv_obj_t* object) {
    if (lv_obj_check_type(object, &lv_slider_class)) return object;
    for (uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
        if (auto* found = find_slider(lv_obj_get_child(object, index))) return found;
    }
    return nullptr;
}

bool has_text(lv_obj_t* object, std::string_view text) {
    if (lv_obj_check_type(object, &lv_label_class)
        && std::string_view{lv_label_get_text(object)}.find(text) != std::string_view::npos) return true;
    for (uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
        if (has_text(lv_obj_get_child(object, index), text)) return true;
    }
    return false;
}

void click(LvglDeckView& view, const char* text) {
    std::string_view target{text};
    if (target == "Play" || target == "Pause") {
        target = find_button(view.object(), LV_SYMBOL_PLAY) ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
    } else if (target == "Performance") {
        target = "Deck";
    } else if (target == "Time mode") {
        target = "ELAPSED / TIME";
    } else if (target == "Zoom +") {
        target = LV_SYMBOL_PLUS;
    } else if (target == "Zoom -") {
        target = LV_SYMBOL_MINUS;
    } else if (target == "Wave: Mono") {
        target = "MONO";
    } else if (target == "Wave: 3-band") {
        target = "3B";
    } else if (target == "Wave: RGB") {
        target = "RGB";
    }
    auto* button = find_button(view.object(), target);
    assert(button);
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
}

struct Client {
    int controls{0}, seeks{0}, loads{0};
    DeckControlType last_control{};
    double last_control_value{0.0};
    musicrat::backends::media::DeckSeekRequest last_seek{};
    bool accept{true};
    static bool control(void* context, DeckControlType type, double value) noexcept {
        auto& self = *static_cast<Client*>(context);
        ++self.controls;
        self.last_control = type;
        self.last_control_value = value;
        return self.accept;
    }
    static bool seek(void* context, const musicrat::backends::media::DeckSeekRequest& request) noexcept {
        auto& self = *static_cast<Client*>(context);
        ++self.seeks;
        self.last_seek = request;
        return self.accept;
    }
    static bool load(void* context, const MediaSelectionResult& selection) noexcept {
        auto& self = *static_cast<Client*>(context);
        ++self.loads;
        assert(selection.error == MEDIA_BROWSE_ERROR_NONE && !selection.resolved_path.empty());
        return self.accept;
    }
};

struct Pointer {
    lv_indev_t* input{lv_indev_create()};
    lv_point_t point{};
    bool pressed{false};
    explicit Pointer(lv_display_t* display) {
        lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(input, display);
        lv_indev_set_mode(input, LV_INDEV_MODE_EVENT);
        lv_indev_set_user_data(input, this);
        lv_indev_set_read_cb(input, [](lv_indev_t* input, lv_indev_data_t* data) {
            const auto& self = *static_cast<Pointer*>(lv_indev_get_user_data(input));
            data->point = self.point;
            data->state = self.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        });
    }
    ~Pointer() { lv_indev_delete(input); }
    void at(int32_t x, int32_t y, bool down) {
        lv_obj_update_layout(lv_display_get_screen_active(lv_indev_get_display(input)));
        point = {x, y};
        pressed = down;
        lv_indev_read(input);
    }
    void tap(int32_t x, int32_t y) { at(x, y, true); at(x, y, false); }
};
} // namespace

int main() {
    using namespace musicrat::backends;
    musicrat::testing::TestDirectory fixture;
    musicrat::testing::make_file(fixture.path / "track.wav");
    lv_init();
    auto handle = create_lvgl_display({.backend = LvglDisplayBackend::Headless,
        .device_path = "", .input_device_path = ""});
    auto* display = handle.display;
    assert(display);
    std::vector<uint32_t> pixels(800 * 480);
    int flushes = 0;
    lv_display_set_user_data(display, &flushes);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, pixels.data(), nullptr,
        static_cast<uint32_t>(pixels.size() * sizeof(uint32_t)), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* value, const lv_area_t*, uint8_t*) {
        ++*static_cast<int*>(lv_display_get_user_data(value));
        lv_display_flush_ready(value);
    });
    const auto color_count = [&](uint32_t color) {
        return std::count_if(pixels.begin(), pixels.end(),
            [color](uint32_t pixel) { return (pixel & 0x00ffffff) == color; });
    };
    {
        media::DirectoryBrowserWorker worker{fixture.path};
        Client client;
        LvglDeckView presentation{lv_display_get_screen_active(display), worker,
            Client::load, Client::control, Client::seek, &client};
        Pointer pointer{display};
        auto* pitch_slider = find_slider(presentation.object());
        assert(pitch_slider);
        assert(lv_slider_get_orientation(pitch_slider) == LV_SLIDER_ORIENTATION_VERTICAL);
        assert(lv_slider_get_min_value(pitch_slider) == -1600
            && lv_slider_get_max_value(pitch_slider) == 1600);
        assert(lv_obj_has_state(pitch_slider, LV_STATE_DISABLED));
        assert(!find_button(presentation.object(), "Seek (paused)"));
        click(presentation, "Play");
        assert(client.seeks == 0 && client.controls == 0);
        assert(has_text(presentation.object(), "--:--.---"));
        media::DeckLoadView view{};
        view.connected = true;
        view.active_name = "track.wav";
        view.playback = {.media_position_frames = 480000, .generation = 3,
            .duration_frames = 2880000, .source_sample_rate_hz = 48000, .track_instance_id = 7};
        presentation.apply(view);
        assert(has_text(presentation.object(), "00:10.000"));
        assert(has_text(presentation.object(), "+0.00%"));
        assert(has_text(presentation.object(), "TRACK BPM --"));
        view.playback.track_bpm = 128;
        view.playback.current_bpm = 131.84;
        view.playback.playback_rate = 1.03;
        view.playback.beat_grid_track_instance_id = 7;
        presentation.apply(view);
        assert(has_text(presentation.object(), "TRACK BPM 128.0"));
        assert(has_text(presentation.object(), "131.8"));
        assert(has_text(presentation.object(), "+3.00%"));
        view.playback.pitch_mode = PLAYBACK_PITCH_MODE_LOCKED;
        presentation.apply(view);
        assert(has_text(presentation.object(), "131.8"));
        assert(has_text(presentation.object(), "PITCH MT"));
        view.playback.beat_grid_track_instance_id = 8;
        presentation.apply(view);
        assert(has_text(presentation.object(), "--.-"));
        view.playback.beat_grid_track_instance_id = 7;
        view.playback.current_bpm = std::numeric_limits<double>::quiet_NaN();
        presentation.apply(view);
        assert(has_text(presentation.object(), "--.-"));
        view.playback.current_bpm = 128;
        view.playback.playback_rate = 1;
        lv_slider_set_value(pitch_slider, 25, LV_ANIM_OFF);
        lv_obj_send_event(pitch_slider, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(client.controls == 1 && client.last_control == DECK_CONTROL_SET_RATE
            && std::abs(client.last_control_value - 1.0025) < 1e-12);
        view.playback.playback_rate = 1.0025;
        view.playback.current_bpm = 128.32;
        presentation.apply(view);
        assert(has_text(presentation.object(), "+0.25%"));
        lv_slider_set_value(pitch_slider, 0, LV_ANIM_OFF);
        lv_obj_send_event(pitch_slider, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(client.controls == 2 && client.last_control == DECK_CONTROL_SET_RATE
            && std::abs(client.last_control_value - 1.0) < 1e-12);
        view.playback.playback_rate = 1;
        view.playback.current_bpm = 128;
        presentation.apply(view);
        const auto controls_before_touch = client.controls;
        pointer.tap(759, 145);
        assert(client.controls > controls_before_touch
            && client.last_control == DECK_CONTROL_SET_RATE
            && client.last_control_value > 1.08);
        view.playback.playback_rate = client.last_control_value;
        presentation.apply(view);
        lv_slider_set_value(pitch_slider, 0, LV_ANIM_OFF);
        lv_obj_send_event(pitch_slider, LV_EVENT_VALUE_CHANGED, nullptr);
        view.playback.playback_rate = 1;
        presentation.apply(view);
        click(presentation, "Time mode");
        assert(has_text(presentation.object(), "REMAIN / TIME"));
        assert(has_text(presentation.object(), "00:50.000"));
        view.playback.duration_frames = 48000ULL * 61 * 60;
        presentation.apply(view);
        assert(has_text(presentation.object(), "60:50.000"));
        view.playback.duration_frames = 2880000;
        presentation.apply(view);
        click(presentation, "Play");
        assert(client.last_control == DECK_CONTROL_PLAY);
        pointer.tap(400, 272);
        assert(client.seeks == 0);
        view.playback.flags = PLAYBACK_STATUS_PLAYING;
        presentation.apply(view);
        assert(has_text(presentation.object(), "Playing confirmed"));
        pointer.tap(400, 272);
        assert(client.seeks == 0);
        assert(has_text(presentation.object(), "Pause to seek"));
        view.playback.flags = 0;
        presentation.apply(view);
        pointer.at(400, 272, true);
        assert(client.seeks == 0);
        assert(has_text(presentation.object(), "Preview:"));
        assert(has_text(presentation.object(), "PREVIEW"));
        pointer.at(400, 272, false);
        assert(client.seeks == 1 && client.last_seek.track_instance_id == 7
            && client.last_seek.generation == 3 && client.last_seek.media_frame == 1441858);
        pointer.tap(450, 272);
        assert(client.seeks == 1);
        click(presentation, "Browse");
        for (int attempt = 0; attempt < 1000 && !find_button(presentation.object(), "track.wav"); ++attempt) {
            presentation.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        click(presentation, "track.wav");
        click(presentation, "Performance");
        view.playback.media_position_frames = client.last_seek.media_frame;
        view.playback.generation = 4;
        view.seek_state = media::DeckSeekState::PositionObserved;
        presentation.apply(view);
        assert(has_text(presentation.object(), "Requested position observed"));
        click(presentation, "Browse");
        click(presentation, "Use selection");
        for (int attempt = 0; attempt < 1000 && client.loads == 0; ++attempt) {
            presentation.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        assert(client.loads == 1);
        click(presentation, "Performance");
        for (int x : {12, 787}) {
            pointer.tap(x, 272);
            assert(client.last_seek.media_frame == (x == 12 ? 0 : view.playback.duration_frames - 1));
            presentation.apply(view);
        }
        pointer.at(300, 156, true);
        pointer.at(260, 156, true);
        ++view.playback.generation;
        presentation.apply(view);
        const auto count = client.seeks;
        pointer.at(260, 156, false);
        assert(client.seeks == count);
        for (uint16_t flag : {PLAYBACK_STATUS_LOOPING, PLAYBACK_STATUS_INVALID, PLAYBACK_STATUS_PLAYING}) {
            view.playback.flags = flag;
            presentation.apply(view);
            pointer.tap(400, 272);
            assert(client.seeks == count);
        }
        view.playback.flags = 0;
        view.connected = false;
        presentation.apply(view);
        assert(has_text(presentation.object(), "--:--.---"));
        view.connected = true;
        view.playback.source_sample_rate_hz = 0;
        presentation.apply(view);
        assert(has_text(presentation.object(), "--:--.---"));
        view.playback.source_sample_rate_hz = 48000;
        view.playback.duration_frames = 0;
        presentation.apply(view);
        assert(has_text(presentation.object(), "--:--.---"));
        pointer.tap(400, 272);
        assert(client.seeks == count);
        lv_obj_update_layout(presentation.object());
        assert(lv_obj_get_scroll_bottom(presentation.object()) == 0);
        assert(has_text(presentation.object(), "Waveform unavailable"));

        const auto wave_path = fixture.path / "overview.wav";
        {
            audio::WavWriter writer;
            assert(writer.open(wave_path.c_str(), 48000, 1));
            auto block = std::make_unique<AudioBlock>();
            block->sample_rate_hz = 48000;
            block->frame_count = 2048;
            block->channel_count = 1;
            for (uint32_t frame = 0; frame < 2048; ++frame) {
                block->channels[0].push_back(frame % 16 == 0 ? 1 : -0.5);
            }
            assert(writer.write(*block));
        }
        view.active_path = wave_path.c_str();
        view.active_asset = media::regular_file_identity(wave_path.c_str());
        view.playback.source_channel_count = 1;
        view.playback.duration_frames = 2048;
        view.playback.media_position_frames = 512;
        presentation.apply(view);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (presentation.waveform_state() == media::WaveformState::Analyzing) {
            assert(std::chrono::steady_clock::now() < deadline);
            presentation.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        assert(presentation.waveform_state() == media::WaveformState::Ready);
        assert(has_text(presentation.object(), "Waveform ready"));
        // Exercise the real draw callback and keep a seek generation from rebuilding the asset.
        lv_refr_now(display);
        assert(flushes > 0 && color_count(0xffe26b) > 50);
        assert(color_count(0x42c8ee) > 100 && color_count(0x236277) > 100);
        assert(std::count_if(pixels.begin() + 150 * 800, pixels.end(),
            [](uint32_t pixel) { return (pixel & 0x00ffffff) == 0xf0f0f0; }) > 100);
        assert(presentation.detail_seconds() == 8);
        assert(presentation.detail_viewport().position(view.playback.media_position_frames) == 0.5);
        const auto controls_before_wave = client.controls;
        const auto seeks_before_wave = client.seeks;
        click(presentation, "Zoom +");
        assert(presentation.detail_seconds() == 4 && presentation.waveform_state() == media::WaveformState::Ready);
        click(presentation, "Zoom +");
        click(presentation, "Zoom +");
        assert(presentation.detail_seconds() == 2);
        for (int attempt = 0; attempt < 8; ++attempt) click(presentation, "Zoom -");
        assert(presentation.detail_seconds() == 32);
        for (int attempt = 0; attempt < 3; ++attempt) click(presentation, "Zoom +");
        assert(presentation.detail_seconds() == 4);
        click(presentation, "Wave: Mono");
        assert(presentation.waveform_mode() == media::WaveformColorMode::ThreeBand);
        lv_refr_now(display);
        assert(color_count(0x42c8ee) == 0);
        assert(color_count(0xff0000) + color_count(0x00ff00) + color_count(0x0000ff) > 100);
        click(presentation, "Wave: 3-band");
        assert(presentation.waveform_mode() == media::WaveformColorMode::Rgb);
        lv_refr_now(display);
        assert(color_count(0x42c8ee) == 0 && color_count(0xffe26b) > 50);
        assert((pixels[229 * 800 + 30] & 0xffffff) == 0x080c12);
        assert((pixels[229 * 800 + 700] & 0xffffff) != 0x080c12);
        {
            media::WaveformWorker color_worker;
            color_worker.submit({.track_instance_id = 1, .path = wave_path.c_str(), .frame_count = 2048,
                .sample_rate_hz = 48000, .channel_count = 1});
            media::WaveformResult expected{};
            while (!color_worker.poll(expected) || expected.state == media::WaveformState::Analyzing) {
                assert(std::chrono::steady_clock::now() < deadline);
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            assert(expected.state == media::WaveformState::Ready && expected.overview);
            const auto color = media::waveform_rgb(expected.overview->column(100, 776));
            assert((pixels[272 * 800 + 112] & 0xffffff) == color);
        }
        click(presentation, "Browse");
        click(presentation, "Performance");
        assert(presentation.waveform_mode() == media::WaveformColorMode::Rgb && presentation.detail_seconds() == 4);
        click(presentation, "Wave: RGB");
        assert(presentation.waveform_mode() == media::WaveformColorMode::Monochrome);
        assert(client.controls == controls_before_wave && client.seeks == seeks_before_wave);
        // The detail playhead remains at the center as source position and zoom change.
        for (const double frame : {0.0, 1000.0, 2048.0}) {
            view.playback.media_position_frames = frame;
            presentation.apply(view);
            assert(presentation.detail_viewport().position(frame) == 0.5);
            lv_refr_now(display);
            unsigned cursor_pixels = 0;
            for (int y = 129; y < 279; ++y) {
                for (int x = 349; x <= 354; ++x) {
                    if ((pixels[y * 800 + x] & 0xffffff) == 0xffe26b) ++cursor_pixels;
                }
            }
            assert(cursor_pixels > 50);
            assert((pixels[229 * 800 + 30] & 0xffffff) == 0x080c12);
            assert((pixels[229 * 800 + 700] & 0xffffff) != 0x080c12);
        }
        ++view.playback.generation;
        view.playback.media_position_frames = 0;
        presentation.apply(view);
        assert(presentation.waveform_state() == media::WaveformState::Ready);
        lv_refr_now(display);
        assert(color_count(0xffe26b) > 50);
        ++view.playback.track_instance_id;
        ++view.playback.duration_frames;
        presentation.apply(view);
        assert(presentation.waveform_state() == media::WaveformState::Analyzing);
        lv_refr_now(display);
        assert(color_count(0x42c8ee) == 0);
        while (presentation.waveform_state() == media::WaveformState::Analyzing) {
            assert(std::chrono::steady_clock::now() < deadline);
            presentation.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        assert(presentation.waveform_state() == media::WaveformState::Failed);
        assert(has_text(presentation.object(), "Waveform failed"));
        view.connected = false;
        presentation.apply(view);
        assert(presentation.waveform_state() == media::WaveformState::Unavailable);
        lv_refr_now(display);
        assert(color_count(0xffe26b) == 0 && color_count(0x42c8ee) == 0);
        view.connected = true;
        view.playback.track_instance_id = 99;
        view.active_path = {};
        view.active_asset.reset();
        presentation.apply(view);
        assert(presentation.waveform_state() == media::WaveformState::Unavailable);
    }
    {
        const auto wave_path = fixture.path / "scroll.wav";
        constexpr uint64_t frames = 192000;
        {
            audio::WavWriter writer;
            assert(writer.open(wave_path.c_str(), 48000, 1));
            auto block = std::make_unique<AudioBlock>();
            for (uint64_t offset = 0; offset < frames;) {
                *block = {};
                block->sample_rate_hz = 48000;
                block->channel_count = 1;
                block->frame_count = static_cast<uint32_t>(std::min<uint64_t>(AudioBlock::MAX_FRAMES, frames - offset));
                for (uint32_t frame = 0; frame < block->frame_count; ++frame) {
                    const auto source = offset + frame;
                    const std::array<double, 3> frequencies{80, 1200, 9000};
                    const double amplitude = 0.2 + static_cast<double>(source % 4096) / 8192;
                    block->channels[0].push_back(static_cast<AudioBlock::Sample>(amplitude * std::sin(
                        2 * std::numbers::pi * frequencies[(source / 997) % 3] * static_cast<double>(source) / 48000)));
                }
                assert(writer.write(*block));
                offset += block->frame_count;
            }
        }
        media::DirectoryBrowserWorker worker{fixture.path};
        Client client;
        LvglDeckView presentation{lv_display_get_screen_active(display), worker,
            Client::load, Client::control, Client::seek, &client};
        media::DeckLoadView view{};
        view.connected = true;
        view.active_path = wave_path.c_str();
        view.active_asset = media::regular_file_identity(wave_path.c_str());
        view.playback = {.media_position_frames = 48000, .generation = 1, .duration_frames = frames,
            .source_sample_rate_hz = 48000, .source_channel_count = 1,
            .flags = PLAYBACK_STATUS_PLAYING, .track_instance_id = 1};
        presentation.apply(view);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (presentation.waveform_state() == media::WaveformState::Analyzing) {
            assert(std::chrono::steady_clock::now() < deadline);
            presentation.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        assert(presentation.waveform_state() == media::WaveformState::Ready);
        click(presentation, "Zoom +");
        click(presentation, "Zoom +");
        assert(presentation.detail_seconds() == 2);
        // The entire rendered envelope translates, rather than reshaping/recoloring between source cells.
        for (const char* mode : {"Wave: Mono", "Wave: 3-band", "Wave: RGB"}) {
            view.playback.media_position_frames = 48000;
            presentation.apply(view);
            lv_refr_now(display);
            const auto reference = pixels;
            const double per_pixel = presentation.detail_viewport().span_frames / 680;
            for (const double fraction : {0.1, 0.3, 0.49, 1.0, 3.2}) {
                view.playback.media_position_frames = 48000 + fraction * per_pixel;
                presentation.apply(view);
                assert(presentation.waveform_state() == media::WaveformState::Ready);
                lv_refr_now(display);
                const auto shift = static_cast<int>(std::round(fraction));
                unsigned colored = 0, changed = 0;
                for (int y = 115; y < 229; ++y) {
                    for (int x = 20; x < 574 - shift; ++x) {
                        if (x >= 346 && x <= 358) continue;
                        assert(pixels[y * 800 + x] == reference[y * 800 + x + shift]);
                        colored += (pixels[y * 800 + x] & 0xffffff) != 0x080c12;
                        changed += pixels[y * 800 + x] != reference[y * 800 + x];
                    }
                }
                assert(colored > 1000 && (shift == 0 || changed > 100));
            }
            click(presentation, mode);
        }
        view.playback.media_position_frames = 48000;
        presentation.apply(view);
        lv_refr_now(display);
        assert((pixels[272 * 800 + 12] & 0xffffff) == 0x9caec0);
        assert((pixels[272 * 800 + 400] & 0xffffff) == 0x9caec0);
        assert((pixels[272 * 800 + 600] & 0xffffff) != 0x9caec0);
        assert(std::count_if(pixels.begin() + 80 * 800, pixels.begin() + 102 * 800,
            [](uint32_t pixel) { return (pixel & 0xffffff) == 0x9caec0; }) > 50);
        view.playback.media_position_frames = 96000;
        presentation.apply(view);
        lv_refr_now(display);
        assert((pixels[272 * 800 + 205] & 0xffffff) == 0x9caec0);
        assert((pixels[272 * 800 + 594] & 0xffffff) == 0x9caec0);
        assert((pixels[272 * 800 + 100] & 0xffffff) != 0x9caec0);
        click(presentation, "Zoom -");
        lv_refr_now(display);
        assert((pixels[272 * 800 + 12] & 0xffffff) == 0x9caec0);
        assert((pixels[272 * 800 + 787] & 0xffffff) == 0x9caec0);
        for (const double frame : {0.0, static_cast<double>(frames)}) {
            view.playback.media_position_frames = frame;
            presentation.apply(view);
            lv_refr_now(display);
            const auto left = frame == 0 ? 12 : 399;
            const auto right = frame == 0 ? 400 : 787;
            const auto outside = frame == 0 ? 700 : 100;
            assert((pixels[272 * 800 + (frame == 0 ? right : left)] & 0xffffff) == 0x9caec0);
            assert((pixels[272 * 800 + outside] & 0xffffff) != 0x9caec0);
        }
        assert(client.controls == 0 && client.seeks == 0 && client.loads == 0);
        view.playback.media_position_frames = 96000;
        view.playback.beat_grid_track_instance_id = view.playback.track_instance_id;
        view.playback.beat_grid.segments.push_back({.start_frame = 24000, .tempo_bpm = 120});
        view.playback.beat_grid.segments.push_back({.start_frame = 120000, .start_beat = 4, .tempo_bpm = 240});
        view.playback.beat_grid.beats_per_bar = 3;
        presentation.apply(view);
        lv_refr_now(display);
        assert(has_text(presentation.object(), "Grid 3/4 (configured)"));
        assert((pixels[120 * 800 + 97] & 0xffffff) == 0xb7c9df);
        assert((pixels[272 * 800 + 109] & 0xffffff) == 0xb7c9df);
        assert((pixels[120 * 800 + 182] & 0xffffff) == 0x61758e);
        assert((pixels[272 * 800 + 206] & 0xffffff) == 0x61758e);
        const auto grid_colors = [&] { return color_count(0xb7c9df) + color_count(0x61758e); };
        assert(grid_colors() > 400);
        const auto beat_pixels = pixels;
        const auto beat_frame = view.playback.media_position_frames;
        for (const double fraction : {0.1, 0.3, 0.49}) {
            view.playback.media_position_frames = beat_frame + fraction * presentation.detail_viewport().span_frames / 680;
            presentation.apply(view);
            lv_refr_now(display);
            for (int y = 115; y < 229; ++y) {
                for (int x = 20; x < 574; ++x) {
                    if (x >= 346 && x <= 358) continue;
                    assert(pixels[y * 800 + x] == beat_pixels[y * 800 + x]);
                }
            }
        }
        view.playback.media_position_frames = beat_frame;
        presentation.apply(view);
        // Metadata changes redraw even while the player position is stationary.
        view.playback.beat_grid.segments[0].tempo_bpm = 0;
        presentation.apply(view);
        lv_refr_now(display);
        assert(grid_colors() == 0 && has_text(presentation.object(), "Grid --"));
        view.playback.beat_grid.segments[0].tempo_bpm = 120;
        ++view.playback.beat_grid_track_instance_id;
        presentation.apply(view);
        lv_refr_now(display);
        assert(grid_colors() == 0);
        --view.playback.beat_grid_track_instance_id;
        view.playback.beat_grid.sample_rate_hz = 44100;
        presentation.apply(view);
        lv_refr_now(display);
        assert(grid_colors() == 0);
        view.playback.beat_grid.sample_rate_hz = 48000;
        view.playback.flags |= PLAYBACK_STATUS_INVALID;
        presentation.apply(view);
        lv_refr_now(display);
        assert(grid_colors() == 0);
        view.playback.flags &= ~PLAYBACK_STATUS_INVALID;
        presentation.apply(view);
        lv_refr_now(display);
        assert(grid_colors() > 400);
        const auto await_grid_waveform = [&] {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (presentation.waveform_state() == media::WaveformState::Analyzing) {
                assert(std::chrono::steady_clock::now() < until);
                presentation.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            assert(presentation.waveform_state() == media::WaveformState::Ready);
            lv_refr_now(display);
            assert(grid_colors() > 400);
        };
        view.session_uncertain = true;
        presentation.apply(view);
        lv_refr_now(display);
        assert(grid_colors() == 0 && has_text(presentation.object(), "Grid --"));
        view.session_uncertain = false;
        presentation.apply(view);
        await_grid_waveform();
        view.connected = false;
        presentation.apply(view);
        lv_refr_now(display);
        assert(grid_colors() == 0 && has_text(presentation.object(), "Grid --"));
        view.connected = true;
        presentation.apply(view);
        await_grid_waveform();
        view.playback.marker_track_instance_id = view.playback.track_instance_id;
        view.playback.markers = {.cue_frame = 24000, .loop_start_frame = 48000, .loop_end_frame = 144000,
            .cue_set = true, .loop_start_set = true, .loop_end_set = true};
        presentation.apply(view);
        lv_refr_now(display);
        assert(has_text(presentation.object(), "Cue 0.500s"));
        assert(has_text(presentation.object(), "stored (readiness unknown)"));
        assert(color_count(0xe88dfa) > 50 && color_count(0xe7b458) > 100);
        assert((pixels[120 * 800 + 97] & 0xffffff) == 0xe88dfa);
        assert((pixels[272 * 800 + 109] & 0xffffff) == 0xe88dfa);
        view.playback.markers.loop_enabled = true;
        view.playback.flags |= PLAYBACK_STATUS_LOOPING;
        presentation.apply(view);
        lv_refr_now(display);
        assert(has_text(presentation.object(), "Loop active"));
        assert(color_count(0x57db87) > 100 && color_count(0xe7b458) == 0);
        view.playback.flags &= ~PLAYBACK_STATUS_LOOPING;
        presentation.apply(view);
        lv_refr_now(display);
        assert(color_count(0x57db87) == 0 && has_text(presentation.object(), "readiness unknown"));
        view.playback.markers.loop_end_frame = 48000;
        presentation.apply(view);
        assert(has_text(presentation.object(), "incomplete / invalid"));
        view.playback.markers.loop_start_set = false;
        view.playback.markers.loop_end_set = false;
        view.playback.markers.cue_frame = frames;
        presentation.apply(view);
        lv_refr_now(display);
        assert(color_count(0xe88dfa) == 0 && color_count(0xe7b458) == 0);
        assert(has_text(presentation.object(), "Cue --"));
        view.playback.markers.cue_frame = 0;
        presentation.apply(view);
        lv_refr_now(display);
        assert(has_text(presentation.object(), "Cue 0.000s") && color_count(0xe88dfa) > 50);
        click(presentation, "Zoom +");
        lv_refr_now(display);
        assert(presentation.detail_seconds() == 2);
        assert(std::any_of(pixels.begin() + 248 * 800, pixels.begin() + 296 * 800,
            [](uint32_t pixel) { return (pixel & 0xffffff) == 0xe88dfa; }));
        click(presentation, "Zoom -");
        ++view.playback.marker_track_instance_id;
        presentation.apply(view);
        lv_refr_now(display);
        assert(has_text(presentation.object(), "Cue --") && color_count(0xe88dfa) == 0);
        view.playback.marker_track_instance_id = view.playback.track_instance_id;
        view.playback.markers = {.loop_end_frame = frames, .loop_end_set = true};
        presentation.apply(view);
        lv_refr_now(display);
        assert(has_text(presentation.object(), "Out 4.000s") && color_count(0xe7b458) > 50);
        view.playback.flags |= PLAYBACK_STATUS_INVALID;
        presentation.apply(view);
        lv_refr_now(display);
        assert(color_count(0xe7b458) == 0);
        view.playback.markers = {};
        view.playback.beat_grid = {};
        view.playback.beat_grid_track_instance_id = 0;
        view.playback.media_position_frames = 96000;
        view.playback.flags = 0;
        view.playback.marker_track_instance_id = view.playback.track_instance_id;
        presentation.apply(view);
        Pointer pointer{display};
        const auto controls_before_cue = client.controls;
        pointer.at(40, 428, true);
        assert(client.controls == controls_before_cue + 1
            && client.last_control == DECK_CONTROL_CUE_BUTTON_DOWN);
        pointer.at(40, 428, false);
        assert(client.controls == controls_before_cue + 2
            && client.last_control == DECK_CONTROL_CUE_BUTTON_UP);
        view.playback.markers = {.cue_frame = 96000, .cue_set = true};
        presentation.apply(view);
        assert(has_text(presentation.object(), "CUE point set"));

        view.playback.media_position_frames = 100000;
        presentation.apply(view);
        pointer.at(40, 428, true);
        assert(client.last_control == DECK_CONTROL_CUE_BUTTON_DOWN);
        pointer.at(40, 428, false);
        assert(client.last_control == DECK_CONTROL_CUE_BUTTON_UP);
        view.playback.markers = {.cue_frame = 100000, .cue_set = true};
        presentation.apply(view);
        assert(has_text(presentation.object(), "CUE point set"));

        pointer.at(40, 428, true);
        assert(client.last_control == DECK_CONTROL_CUE_BUTTON_DOWN);
        view.playback.flags = PLAYBACK_STATUS_PLAYING;
        view.playback.media_position_frames = 100100;
        presentation.apply(view);
        pointer.at(40, 428, false);
        assert(client.last_control == DECK_CONTROL_CUE_BUTTON_UP);
        view.playback.flags = 0;
        view.playback.media_position_frames = 100000;
        presentation.apply(view);
        assert(has_text(presentation.object(), "CUE preview returned"));

        view.playback.flags = PLAYBACK_STATUS_PLAYING;
        view.playback.media_position_frames = 110000;
        presentation.apply(view);
        pointer.at(40, 428, true);
        assert(client.last_control == DECK_CONTROL_CUE_BUTTON_DOWN);
        assert(has_text(presentation.object(), "Returning to CUE point"));
        view.playback.flags = 0;
        view.playback.media_position_frames = 100000;
        pointer.at(40, 428, false);
        assert(client.last_control == DECK_CONTROL_CUE_BUTTON_UP);
        presentation.apply(view);
        assert(has_text(presentation.object(), "Returned to CUE point"));

        view.playback.flags = PLAYBACK_STATUS_PLAYING;
        view.playback.media_position_frames = 110000;
        presentation.apply(view);
        pointer.at(40, 428, true);
        const auto hold_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
        while (client.last_control != DECK_CONTROL_CUE_BUTTON_HOLD) {
            assert(std::chrono::steady_clock::now() < hold_deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
            presentation.poll();
        }
        view.playback.flags = PLAYBACK_STATUS_PLAYING;
        view.playback.media_position_frames = 100100;
        presentation.apply(view);
        pointer.at(40, 428, false);
        assert(client.last_control == DECK_CONTROL_CUE_BUTTON_UP);
        view.playback.flags = 0;
        view.playback.media_position_frames = 100000;
        presentation.apply(view);
        assert(has_text(presentation.object(), "CUE preview returned"));

        view.playback.flags = PLAYBACK_STATUS_PLAYING;
        view.playback.markers = {};
        presentation.apply(view);
        assert(lv_obj_has_state(find_button(presentation.object(), "CUE"), LV_STATE_DISABLED));
        view.playback.flags = 0;
        view.playback.markers = {};
        view.playback.media_position_frames = 96000;
        presentation.apply(view);
        const auto span = presentation.detail_viewport().span_frames;
        pointer.tap(400, 156);
        assert(client.seeks == 0);
        pointer.at(400, 156, true);
        pointer.at(300, 156, true);
        assert(client.seeks == 0 && has_text(presentation.object(), "PREVIEW"));
        assert(presentation.detail_viewport().position(96000) < 0.5);
        pointer.at(300, 156, false);
        assert(client.seeks == 1 && client.last_seek.media_frame == static_cast<uint64_t>(std::round(96000 + 100 * span / 679)));
        assert(presentation.detail_viewport().position(96000) == 0.5);
        presentation.apply(view);
        pointer.at(400, 156, true);
        pointer.at(500, 156, true);
        pointer.at(500, 156, false);
        assert(client.seeks == 2 && client.last_seek.media_frame == static_cast<uint64_t>(std::round(96000 - 100 * span / 679)));
        presentation.apply(view);
        // Drag inside the overview window preserves the grab offset rather than jumping to the pointer.
        pointer.at(420, 272, true);
        pointer.at(470, 272, true);
        pointer.at(470, 272, false);
        assert(client.seeks == 3 && client.last_seek.media_frame == static_cast<uint64_t>(std::round(96000 + 50.0 * frames / 775)));
        presentation.apply(view);
        const auto before_cancel = client.seeks;
        pointer.at(400, 156, true);
        pointer.at(300, 156, true);
        pointer.at(300, 350, true);
        pointer.at(300, 156, true);
        pointer.at(300, 156, false);
        assert(client.seeks == before_cancel && !has_text(presentation.object(), "PREVIEW"));
        for (int reason = 0; reason < 9; ++reason) {
            presentation.apply(view);
            pointer.at(400, 156, true);
            pointer.at(350, 156, true);
            auto changed = view;
            switch (reason) {
            case 0: changed.playback.flags = PLAYBACK_STATUS_PLAYING; break;
            case 1: ++changed.playback.generation; break;
            case 2: ++changed.playback.track_instance_id; break;
            case 3: changed.connected = false; break;
            case 4: changed.operation_pending = true; break;
            case 5: changed.session_uncertain = true; break;
            case 6: changed.playback.transport_sync_mode = TRANSPORT_SYNC_BEAT; break;
            case 7: changed.playback.flags = PLAYBACK_STATUS_LOOPING; break;
            case 8: changed.playback.decode_error = PLAYBACK_DECODE_ERROR_SEEK_FAILED; break;
            }
            presentation.apply(changed);
            pointer.at(350, 156, false);
            assert(client.seeks == before_cancel && !has_text(presentation.object(), "PREVIEW"));
        }
        presentation.apply(view);
        pointer.at(400, 156, true);
        pointer.at(350, 156, true);
        click(presentation, "Zoom +");
        pointer.at(350, 156, false);
        assert(client.seeks == before_cancel);
        pointer.at(400, 156, true);
        pointer.at(350, 156, true);
        click(presentation, "Browse");
        pointer.at(350, 156, false);
        click(presentation, "Performance");
        assert(client.seeks == before_cancel);
        client.accept = false;
        pointer.tap(100, 272);
        assert(client.seeks == before_cancel + 1);
        assert(has_text(presentation.object(), "worker busy; retry"));
        client.accept = true;
        pointer.tap(100, 272);
        assert(client.seeks == before_cancel + 2);
        assert(has_text(presentation.object(), "Seek queued"));
        pointer.tap(200, 272);
        assert(client.seeks == before_cancel + 2);
        view.connected = false;
        presentation.apply(view);
        lv_refr_now(display);
        assert(grid_colors() == 0 && has_text(presentation.object(), "Grid --"));
    }
    lv_display_delete(display);
}
