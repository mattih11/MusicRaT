#include <musicrat/backends/audio/wav_writer.hpp>
#include <musicrat/backends/media/directory_browser.hpp>
#include <musicrat/modules/audio_file_player.hpp>
#include <musicrat/modules/lvgl_deck.hpp>

#include "../support/directory_fixture.hpp"
#include <rfl/json.hpp>

#include <cassert>
#include <atomic>
#include <chrono>
#include <memory>
#include <source_location>
#include <cstdio>
#include <thread>

namespace {
using namespace CommRaT::Messages;
using State = musicrat::control::SelectionLoad::State;

class GridProbeDeck : public CommRaT::LvglDeck {
public:
    using CommRaT::LvglDeck::LvglDeck;
    std::atomic<bool> grid_observed{false};
    std::atomic<bool> cue_set{false};
    std::atomic<uint64_t> cue_frame{0};

protected:
    void process(const commrat::Synced<PlaybackStatusBlock>& status, DeckControlEventBlock& output) override {
        CommRaT::LvglDeck::process(status, output);
        if (status.is_fresh()) {
            const auto& data = status.value();
            if (data.track_instance_id != 0
                && data.beat_grid_track_instance_id == data.track_instance_id
                && data.beat_grid.segments.size() == MAX_BEAT_GRID_SEGMENTS
                && data.beat_grid.segments.back().start_beat == 62) {
                grid_observed.store(true, std::memory_order_release);
            }
            if (data.marker_track_instance_id == data.track_instance_id) {
                cue_frame.store(data.markers.cue_frame, std::memory_order_relaxed);
                cue_set.store(data.markers.cue_set, std::memory_order_release);
            }
        }
    }
};

template<typename Predicate>
void await(Predicate predicate, const std::source_location location = std::source_location::current()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            std::fprintf(stderr, "Deck RPC timeout at %s:%u\n", location.file_name(), location.line());
            assert(false && "deck RPC deadline");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
}

void make_track(const std::filesystem::path& path) {
    auto block = std::make_unique<AudioBlock>();
    block->sample_rate_hz = 48000;
    block->channel_count = 1;
    block->frame_count = 480;
    for (uint32_t index = 0; index < block->frame_count; ++index) block->channels[0].push_back(0.1);
    musicrat::backends::audio::WavWriter writer;
    assert(writer.open(path.c_str(), 48000, 1));
    for (int index = 0; index < 6000; ++index) assert(writer.write(*block));
}
} // namespace

int main() {
    musicrat::testing::TestDirectory fixture;
    make_track(fixture.path / "first.wav");
    make_track(fixture.path / "second.wav");
    musicrat::testing::make_file(fixture.path / "corrupt.wav");
    musicrat::backends::media::DirectoryBrowser browser;
    assert(browser.configure(fixture.path) == MEDIA_BROWSE_ERROR_NONE);
    MediaDirectoryPage page{};
    browser.browse({.request_id = 1}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE);

    commrat::ModuleConfig player_config{};
    player_config.name = "DeckRpcPlayer";
    player_config.outputs = commrat::MultiOutputConfig{.addresses = {
        {.system_id = 171, .instance_id = 19}, {.system_id = 172, .instance_id = 19}}};
    player_config.inputs = commrat::MultiInputConfig{.sources = {{
        .system_id = 173, .instance_id = 19, .lifecycle_address = 0, .is_primary = false}},
        .history_buffer_size = 8, .sync_tolerance = std::chrono::milliseconds{100}};
    player_config.period = std::chrono::milliseconds{10};
    CommRaT::Parameters::FilePlayer params{};
    params.path = (fixture.path / "first.wav").c_str();
    params.autoplay = false;
    for (uint32_t index = 0; index < MAX_BEAT_GRID_SEGMENTS; ++index) {
        params.beat_grid.segments.push_back({
            .start_frame = index * uint64_t{48000}, .start_beat = index * 2.0, .tempo_bpm = 120});
    }
    player_config.params = rfl::json::read<rfl::Generic>(rfl::json::write(params)).value();
    auto player = std::make_unique<CommRaT::AudioFilePlayer>(player_config);

    commrat::ModuleConfig deck_config{};
    deck_config.name = "DeckRpcDisplay";
    deck_config.outputs = commrat::SimpleOutputConfig{.system_id = 173, .instance_id = 19};
    deck_config.inputs = commrat::MultiInputConfig{.sources = {{
        .system_id = 172, .instance_id = 19, .lifecycle_address = 0, .is_primary = false}},
        .history_buffer_size = 8, .sync_tolerance = std::chrono::milliseconds{100}};
    deck_config.remotes = {{.system_id = 171, .instance_id = 19}};
    deck_config.period = std::chrono::milliseconds{10};
    deck_config.params = rfl::json::read<rfl::Generic>(
        "{\"media_root\":\"" + fixture.path.string() + "\",\"backend\":0}").value();
    auto deck = std::make_unique<GridProbeDeck>(deck_config);
    player->start();
    deck->start();
    await([&] { return deck->connected(); });
    await([&] { return deck->grid_observed.load(std::memory_order_acquire); });
    uint64_t previous_load = 0;
    auto load = [&](const char* path, MediaLoadState expected) {
        MediaSelectionResult selection{};
        browser.resolve_selection({
            .request_id = previous_load + 2, .snapshot_id = page.snapshot_id, .relative_path = path,
        }, selection);
        assert(selection.error == MEDIA_BROWSE_ERROR_NONE);
        assert(deck->queue_selection(selection));
        assert(!deck->queue_selection(selection));
        await([&] {
            return deck->load_state() == State::Terminal
                && deck->load_request_id() > previous_load && deck->load_result() == expected;
        });
        previous_load = deck->load_request_id();
    };
    load("first.wav", MEDIA_LOAD_COMMITTED);
    const auto first = deck->active_track();
    assert(first != 0 && first == deck->load_request_id());
    await([&] { return deck->waveform_state() == musicrat::backends::media::WaveformState::Ready; });
    assert(deck->queue_control(DECK_CONTROL_PLAY));
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) != 0; });
    load("second.wav", MEDIA_LOAD_PROTECTED);
    assert(deck->active_track() == first);
    assert(deck->queue_control(DECK_CONTROL_PAUSE));
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) == 0; });
    load("second.wav", MEDIA_LOAD_COMMITTED);
    const auto second = deck->active_track();
    assert(second != first && second == deck->load_request_id());
    await([&] { return deck->waveform_state() == musicrat::backends::media::WaveformState::Ready; });
    load("corrupt.wav", MEDIA_LOAD_FAILED);
    assert(deck->active_track() == second);
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) == 0; });
    const auto before_seek = deck->generation();
    assert(deck->queue_seek({.track_instance_id = second, .generation = before_seek, .media_frame = 480000}));
    assert(!deck->queue_selection({}));
    await([&] { return deck->seek_state() == musicrat::backends::media::DeckSeekState::PositionObserved; });
    assert(deck->active_track() == second && deck->generation() > before_seek && deck->media_frame() == 480000);
    assert(!deck->queue_control(DECK_CONTROL_SET_CUE, 480000.5));
    const auto cue_frame = deck->media_frame();
    // One second exceeds the four-chunk decode-ahead window, so cue audio is no longer resident.
    constexpr uint64_t evicted_distance = 48000;
    assert(deck->queue_control(DECK_CONTROL_SET_CUE, static_cast<double>(cue_frame)));
    await([&] { return deck->cue_set.load(std::memory_order_acquire)
        && deck->cue_frame.load(std::memory_order_relaxed) == cue_frame; });
    assert(deck->queue_control(DECK_CONTROL_CUE_BUTTON_DOWN));
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) != 0; });
    await([&] { return deck->media_frame() > cue_frame + evicted_distance; });
    assert(deck->queue_control(DECK_CONTROL_CUE_BUTTON_UP));
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) == 0
        && deck->media_frame() == cue_frame; });
    assert(deck->queue_control(DECK_CONTROL_PLAY));
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) != 0; });
    await([&] { return deck->media_frame() > cue_frame + evicted_distance; });
    assert(deck->queue_control(DECK_CONTROL_CUE_BUTTON_DOWN));
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) == 0
        && deck->media_frame() == cue_frame; });
    assert(deck->queue_control(DECK_CONTROL_CUE_BUTTON_UP));
    assert(deck->queue_control(DECK_CONTROL_PLAY));
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) != 0; });
    await([&] { return deck->media_frame() > cue_frame + evicted_distance; });
    assert(deck->queue_control(DECK_CONTROL_CUE_BUTTON_DOWN));
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) == 0
        && deck->media_frame() == cue_frame; });
    assert(deck->queue_control(DECK_CONTROL_CUE_BUTTON_HOLD));
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) != 0
        && deck->media_frame() > cue_frame + evicted_distance; });
    assert(deck->queue_control(DECK_CONTROL_CUE_BUTTON_UP));
    await([&] { return (deck->playback_flags() & PLAYBACK_STATUS_PLAYING) == 0
        && deck->media_frame() == cue_frame; });
    player->stop();
    await([&] { return !deck->connected(); });
    deck->stop();
}
