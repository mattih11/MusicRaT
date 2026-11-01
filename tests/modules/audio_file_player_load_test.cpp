#include <musicrat/backends/audio/wav_writer.hpp>
#include <musicrat/modules/audio_file_player.hpp>
#include <musicrat/backends/media/directory_browser_worker.hpp>
#include <musicrat/control/selection_load.hpp>

#include "../support/directory_fixture.hpp"

#include <rfl/json.hpp>

#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

namespace {

using namespace CommRaT::Messages;

class Player : public CommRaT::AudioFilePlayer {
public:
    using AudioFilePlayer::AudioFilePlayer;
    using AudioFilePlayer::handle_load;
    using AudioFilePlayer::handle_seek;
    using AudioFilePlayer::handle_unload;
    using AudioFilePlayer::on_start;
    using AudioFilePlayer::on_stop;
    using AudioFilePlayer::on_enable;
    using AudioFilePlayer::on_disable;
    using AudioFilePlayer::process;
};

void make_track(const std::filesystem::path& path, AudioBlock::Sample sample) {
    auto block = std::make_unique<AudioBlock>();
    block->sample_rate_hz = 48000;
    block->channel_count = 1;
    block->frame_count = 960;
    for (uint32_t frame = 0; frame < block->frame_count; ++frame) {
        block->channels[0].push_back(sample);
    }
    musicrat::backends::audio::WavWriter writer{};
    assert(writer.open(path.c_str(), 48000, 1));
    for (uint32_t index = 0; index < 512; ++index) assert(writer.write(*block));
}

void make_click_track(const std::filesystem::path& path, double bpm) {
    auto block = std::make_unique<AudioBlock>();
    block->sample_rate_hz = 48000;
    block->channel_count = 1;
    block->frame_count = 960;
    const auto stride = static_cast<uint64_t>(std::llround(48000.0 * 60.0 / bpm));
    musicrat::backends::audio::WavWriter writer{};
    assert(writer.open(path.c_str(), 48000, 1));
    uint64_t frame = 0;
    for (uint32_t index = 0; index < 1200; ++index) {
        block->channels[0].clear();
        for (uint32_t sample = 0; sample < block->frame_count; ++sample) {
            const auto click = ((frame + sample) % stride) < 240 ? 0.9 : 0.0;
            block->channels[0].push_back(static_cast<AudioBlock::Sample>(click));
        }
        assert(writer.write(*block));
        frame += block->frame_count;
    }
}

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.0001;
}

} // namespace

int main() {
    musicrat::testing::TestDirectory fixture;
    const auto first = fixture.path / "first.wav";
    const auto second = fixture.path / "second.wav";
    const auto analyzed = fixture.path / "analyzed.wav";
    make_track(first, 0.25);
    make_track(second, -0.5);
    make_click_track(analyzed, 128.0);
    musicrat::testing::make_file(fixture.path / "corrupt.wav");

    commrat::ModuleConfig config{};
    config.name = "PreparedFilePlayerTest";
    config.outputs = commrat::MultiOutputConfig{.addresses = {
        {.system_id = 141, .instance_id = 1}, {.system_id = 143, .instance_id = 1}}};
    config.inputs = commrat::MultiInputConfig{
        .sources = {{.system_id = 142, .instance_id = 1, .lifecycle_address = 0, .is_primary = false}},
        .history_buffer_size = 8, .sync_tolerance = std::chrono::milliseconds{20}};
    config.period = std::chrono::milliseconds{10};
    config.params = rfl::json::read<rfl::Generic>(
        "{\"output_sample_rate_hz\":48000.0,\"autoplay\":true}").value();
    auto player = std::make_unique<Player>(config);
    auto output = std::make_unique<AudioBlock>();
    PlaybackStatusBlock status{};
    const commrat::Synced<TransportBlock> transport{};
    auto process = [&] {
        player->process(commrat::Synced<DeckControlEventBlock>{}, transport, *output, status);
    };
    auto await = [&](const auto& predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        do {
            assert(std::chrono::steady_clock::now() < deadline);
            process();
            if (predicate()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        } while (true);
    };
    auto load = [&](const std::filesystem::path& path) {
        LoadMedia::Reply reply{};
        LoadMedia command{};
        command.path = path.c_str();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        do {
            player->handle_load(command, reply);
            if (reply.accepted) return reply.request_id;
            assert(reply.rejection == MEDIA_LOAD_REJECTION_BUSY);
            assert(std::chrono::steady_clock::now() < deadline);
            process();
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        } while (true);
    };
    auto control = [&](DeckControlType type, double value = 0) {
        DeckControlEventBlock block{};
        block.events.push_back({.type = type, .value = value});
        player->process(commrat::Synced<DeckControlEventBlock>{block, true},
            transport, *output, status);
    };
    auto await_sample = [&](AudioBlock::Sample sample) {
        await([&] {
            return (status.flags & PLAYBACK_STATUS_UNDERRUN) == 0
                && output->frame_count != 0 && !output->channels[0].empty()
                && near(output->channels[0][0], sample);
        });
    };

    LoadMedia::Reply reply{};
    player->handle_load({}, reply);
    assert(!reply.accepted && reply.request_id == 0
        && reply.rejection == MEDIA_LOAD_REJECTION_INVALID_PATH);
    player->handle_load({
        .path = sertial::fixed_string<512>{std::string_view{"bad\0path", 8}},
    }, reply);
    assert(!reply.accepted && reply.rejection == MEDIA_LOAD_REJECTION_INVALID_PATH);
    const auto first_id = load(first);
    player->handle_load({.path = second.c_str()}, reply);
    assert(!reply.accepted && reply.rejection == MEDIA_LOAD_REJECTION_BUSY);
    SeekMedia::Reply seek{};
    player->handle_seek({}, seek);
    assert(!seek.accepted);
    player->on_start();
    assert(player->on_enable() == commrat::LifecycleResult::Success);
    await([&] { return status.load_request_id == first_id && status.load_state == MEDIA_LOAD_COMMITTED; });
    assert(status.track_instance_id == first_id && status.source_sample_rate_hz == 48000);
    assert(output->channel_count == 1 && near(output->channels[0][0], 0.25));
    const auto original_generation = status.generation;
    control(DECK_CONTROL_SET_CUE, 0);
    control(DECK_CONTROL_SET_LOOP_START, 100);
    control(DECK_CONTROL_SET_LOOP_END, 200);
    assert(status.marker_track_instance_id == first_id);
    assert(status.markers.cue_set && status.markers.cue_frame == 0);
    assert(status.markers.loop_start_set && status.markers.loop_start_frame == 100);
    assert(status.markers.loop_end_set && status.markers.loop_end_frame == 200);
    assert(!status.markers.loop_enabled);

    const auto failed_id = load(fixture.path / "missing.wav");
    await([&] { return status.load_request_id == failed_id && status.load_state == MEDIA_LOAD_FAILED; });
    assert(status.track_instance_id == first_id && status.generation == original_generation);
    assert(status.load_decode_error != PLAYBACK_DECODE_ERROR_NONE);
    assert(status.decode_error == PLAYBACK_DECODE_ERROR_NONE);
    assert(status.markers.cue_set && status.markers.loop_end_set);
    await_sample(0.25);
    assert(near(output->channels[0][0], 0.25));

    const auto corrupt_id = load(fixture.path / "corrupt.wav");
    await([&] { return status.load_request_id == corrupt_id && status.load_state == MEDIA_LOAD_FAILED; });
    assert(status.track_instance_id == first_id && status.generation == original_generation);
    await_sample(0.25);
    const auto protected_id = load(second);
    await([&] { return status.load_request_id == protected_id && status.load_state == MEDIA_LOAD_PROTECTED; });
    await_sample(0.25);
    assert(status.track_instance_id == first_id && status.generation == original_generation);
    assert(near(output->channels[0][0], 0.25));
    musicrat::backends::media::DirectoryBrowserWorker browser{fixture.path};
    using Submission = musicrat::backends::media::DirectoryBrowserWorker::Submission;
    assert(browser.submit(MediaDirectoryQuery{.request_id = 1}) == Submission::Accepted);
    MediaDirectoryPage page{};
    await([&] { return browser.poll(page); });
    assert(page.error == MEDIA_BROWSE_ERROR_NONE);
    musicrat::control::SelectionLoad selection_load;
    auto select_and_load = [&] {
        assert(browser.submit(MediaSelectionRequest{
            .request_id = 2, .snapshot_id = page.snapshot_id, .relative_path = "second.wav",
        }) == Submission::Accepted);
        MediaSelectionResult selection{};
        await([&] { return browser.poll(selection); });
        LoadMedia command{};
        assert(selection_load.begin(selection, command));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        LoadMedia::Reply accepted{};
        do {
            player->handle_load(command, accepted);
            if (accepted.accepted) break;
            assert(accepted.rejection == MEDIA_LOAD_REJECTION_BUSY);
            assert(std::chrono::steady_clock::now() < deadline);
            process();
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        } while (true);
        assert(selection_load.accept(accepted));
        assert(selection_load.state() == musicrat::control::SelectionLoad::State::AwaitingCompletion);
        await([&] {
            static_cast<void>(selection_load.observe(status));
            return selection_load.state() == musicrat::control::SelectionLoad::State::Terminal;
        });
        return accepted.request_id;
    };
    static_cast<void>(select_and_load());
    assert(selection_load.load_state() == MEDIA_LOAD_PROTECTED && status.track_instance_id == first_id);
    control(DECK_CONTROL_PAUSE);
    assert((status.flags & PLAYBACK_STATUS_PLAYING) == 0);
    const auto second_id = select_and_load();
    assert(selection_load.load_state() == MEDIA_LOAD_COMMITTED);
    assert(status.track_instance_id == second_id && status.generation > original_generation);
    assert(status.marker_track_instance_id == second_id && status.markers == DeckMarkerState{});
    assert((status.flags & PLAYBACK_STATUS_PLAYING) == 0);
    control(DECK_CONTROL_PLAY);
    assert(near(output->channels[0][0], -0.5));

    // Lifecycle unload wins over queued preparation; an already adopted protection result is terminal.
    const auto cancelled_id = load(first);
    player->on_disable();
    await([&] {
        return status.load_request_id == cancelled_id
            && (status.load_state == MEDIA_LOAD_CANCELLED || status.load_state == MEDIA_LOAD_PROTECTED)
            && status.track_instance_id == 0;
    });
    assert(status.worker_state == PLAYBACK_WORKER_CLOSED);
    assert(status.marker_track_instance_id == 0 && status.markers == DeckMarkerState{});
    player->on_stop();

    // Stop also cancels a queued request without a worker or render callback.
    const auto stopped_id = load(first);
    player->on_stop();
    player->on_start();
    assert(player->on_enable() == commrat::LifecycleResult::Success);
    await([&] {
        return status.load_request_id == stopped_id && status.load_state == MEDIA_LOAD_CANCELLED;
    });
    assert(status.track_instance_id == 0);
    const auto recovered_id = load(second);
    await([&] {
        return status.load_request_id == recovered_id && status.load_state == MEDIA_LOAD_COMMITTED;
    });
    assert(status.track_instance_id == recovered_id);
    player->on_stop();

    config.name = "UnprotectedFilePlayerTest";
    config.params = rfl::json::read<rfl::Generic>(
        "{\"output_sample_rate_hz\":48000.0,\"autoplay\":true,"
        "\"protect_playing_loads\":false}").value();
    player = std::make_unique<Player>(config);
    player->on_start();
    assert(player->on_enable() == commrat::LifecycleResult::Success);
    const auto unprotected_first_id = load(first);
    await([&] {
        return status.load_request_id == unprotected_first_id && status.load_state == MEDIA_LOAD_COMMITTED;
    });
    const auto unprotected_second_id = load(second);
    await([&] {
        return status.load_request_id == unprotected_second_id && status.load_state == MEDIA_LOAD_COMMITTED;
    });
    assert(status.track_instance_id == unprotected_second_id);
    assert((status.flags & PLAYBACK_STATUS_PLAYING) != 0);
    assert(near(output->channels[0][0], -0.5));
    player->on_stop();

    config.name = "GridTempoFilePlayerTest";
    config.params = rfl::json::read<rfl::Generic>(
        "{\"path\":\"" + first.string()
        + "\",\"output_sample_rate_hz\":48000.0,\"autoplay\":false,\"initial_rate\":1.03,"
          "\"beat_grid\":{\"sample_rate_hz\":48000,\"segments\":["
          "{\"start_frame\":0,\"start_beat\":0,\"tempo_bpm\":120},"
          "{\"start_frame\":48000,\"start_beat\":2,\"tempo_bpm\":128}]}}").value();
    player = std::make_unique<Player>(config);
    player->on_start();
    assert(player->on_enable() == commrat::LifecycleResult::Success);
    await([&] { return status.track_bpm > 0; });
    const auto grid_track = status.track_instance_id;
    assert(status.beat_grid_track_instance_id == grid_track);
    assert(near(status.track_bpm, 120) && near(status.current_bpm, 123.6));
    const auto published_grid = status.beat_grid;
    assert(published_grid.segments.size() == 2 && published_grid.sample_rate_hz == 48000);
    assert(published_grid.segments[0].tempo_bpm == 120
        && published_grid.segments[1].start_frame == 48000
        && published_grid.segments[1].start_beat == 2
        && published_grid.segments[1].tempo_bpm == 128);
    assert(status.transport_sync_mode == TRANSPORT_SYNC_OFF);
    const auto before_grid_seek = status.generation;
    await([&] {
        player->handle_seek({.media_frame = 48000}, seek);
        return seek.accepted;
    });
    await([&] { return status.generation > before_grid_seek && status.media_position_frames == 48000; });
    assert(near(status.track_bpm, 128) && near(status.current_bpm, 131.84));
    assert(status.beat_grid == published_grid);
    DeckControlEventBlock tempo_change{};
    tempo_change.events.push_back({.type = DECK_CONTROL_SET_RATE, .value = 0.97});
    player->process(commrat::Synced<DeckControlEventBlock>{tempo_change, true}, transport, *output, status);
    assert(near(status.track_bpm, 128) && near(status.current_bpm, 124.16));
    const auto grid_failed = load(fixture.path / "missing.wav");
    await([&] { return status.load_request_id == grid_failed && status.load_state == MEDIA_LOAD_FAILED; });
    assert(status.beat_grid_track_instance_id == grid_track && near(status.track_bpm, 128));
    assert(status.beat_grid == published_grid);
    control(DECK_CONTROL_PLAY);
    const auto grid_protected = load(second);
    await([&] { return status.load_request_id == grid_protected && status.load_state == MEDIA_LOAD_PROTECTED; });
    assert(status.beat_grid_track_instance_id == grid_track && near(status.track_bpm, 128));
    assert(status.beat_grid == published_grid);
    control(DECK_CONTROL_PAUSE);
    const auto no_grid = load(second);
    await([&] { return status.track_instance_id == no_grid; });
    assert(status.beat_grid_track_instance_id == 0 && status.track_bpm == 0 && status.current_bpm == 0);
    assert(status.beat_grid.segments.empty());
    player->on_stop();

    for (const auto* grid : {
        "{\"sample_rate_hz\":44100,\"segments\":[{\"tempo_bpm\":120}]}",
        "{\"sample_rate_hz\":48000,\"segments\":[{\"tempo_bpm\":0}]}",
        "{\"sample_rate_hz\":48000,\"segments\":[]}"}) {
        config.params = rfl::json::read<rfl::Generic>(
            "{\"path\":\"" + first.string() + "\",\"autoplay\":false,\"beat_grid\":" + grid + "}").value();
        player = std::make_unique<Player>(config);
        player->on_start();
        assert(player->on_enable() == commrat::LifecycleResult::Success);
        await([&] { return status.track_instance_id != 0 && status.source_sample_rate_hz != 0; });
        assert(status.beat_grid_track_instance_id == 0 && status.track_bpm == 0 && status.current_bpm == 0);
        assert(status.beat_grid.segments.empty());
        player->on_stop();
        status = {};
    }

    config.params = rfl::json::read<rfl::Generic>(
        "{\"output_sample_rate_hz\":48000.0,\"autoplay\":false}").value();
    player = std::make_unique<Player>(config);
    player->on_start();
    assert(player->on_enable() == commrat::LifecycleResult::Success);
    const auto analyzed_id = load(analyzed);
    await([&] { return status.load_request_id == analyzed_id && status.load_state == MEDIA_LOAD_COMMITTED; });
    await([&] { return status.beat_grid_track_instance_id == analyzed_id && status.track_bpm > 0; });
    assert(status.beat_grid_track_instance_id == analyzed_id);
    assert(status.track_bpm >= 70.0 && status.track_bpm <= 180.0);
    assert(near(status.current_bpm, status.track_bpm));
    assert(status.beat_grid.flags == BEAT_GRID_FLAG_ANALYZED);
    assert(status.beat_grid.segments.size() == 1 && near(status.beat_grid.segments.front().tempo_bpm, status.track_bpm));
    player->on_stop();
}
