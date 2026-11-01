#include <musicrat/backends/media/prepared_playback_coordinator.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <thread>

namespace {

thread_local bool rendering = false;
uint64_t render_allocations = 0;
using namespace musicrat::backends::media;
using namespace CommRaT::Messages;

struct Metadata {
    uint32_t sample_rate_hz{48000};
    uint16_t channel_count{1};
    uint64_t frame_count{100000};
};

class FakeDecoder {
public:
    static constexpr MediaCodec codec = MediaCodec::PcmWav;

    bool open(const char* path) noexcept {
        assert(!rendering);
        frame_ = 0;
        error_ = DecodeError::None;
        if (std::strcmp(path, "missing") == 0) {
            error_ = DecodeError::FileOpen;
            return false;
        }
        corrupt_ = std::strcmp(path, "corrupt") == 0;
        empty_ = std::strcmp(path, "empty") == 0;
        partial_ = std::strcmp(path, "partial") == 0;
        sample_ = std::strcmp(path, "a") == 0 ? 0.25 : -0.5;
        return true;
    }

    void close() noexcept { assert(!rendering); }
    bool seek_frame(uint64_t frame) noexcept {
        assert(!rendering);
        frame_ = frame;
        return frame < metadata_.frame_count;
    }
    uint64_t current_frame() const noexcept { return frame_; }
    DecodeError last_error() const noexcept { return error_; }
    const Metadata& metadata() const noexcept { return metadata_; }

    DecodeResult read(AudioBlock& block, uint32_t frames) noexcept {
        assert(!rendering);
        block = {};
        if (corrupt_ || (partial_ && frame_ != 0)) {
            error_ = DecodeError::CorruptData;
            return DecodeResult::Error;
        }
        if (empty_ || frame_ == metadata_.frame_count) return DecodeResult::EndOfStream;
        block.sample_rate_hz = metadata_.sample_rate_hz;
        block.channel_count = 1;
        block.frame_count = static_cast<uint32_t>(std::min<uint64_t>(
            frames, metadata_.frame_count - frame_));
        for (uint32_t index = 0; index < block.frame_count; ++index) {
            block.channels[0].push_back(sample_);
        }
        frame_ += block.frame_count;
        return DecodeResult::Data;
    }

private:
    Metadata metadata_{};
    uint64_t frame_{0};
    AudioBlock::Sample sample_{0};
    bool corrupt_{false};
    bool empty_{false};
    bool partial_{false};
    DecodeError error_{DecodeError::None};
};

} // namespace

void* operator new(std::size_t size) {
    if (rendering) ++render_allocations;
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc{};
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
    using Player = PreparedPlaybackCoordinator<FakeDecoder, 4>;
    auto player = std::make_unique<Player>(64, 4);
    auto output = std::make_unique<AudioBlock>();
    auto render = [&](const DeckControlEventBlock* controls = nullptr) {
        rendering = true;
        const auto result = player->render(48000, 8, 100, 1, *output, controls);
        const auto markers = player->markers();
        assert(!markers.loop_enabled || markers.loop_start_frame < markers.loop_end_frame);
        rendering = false;
        assert(render_allocations == 0);
        return result;
    };
    player->set_playing(true);
    assert(player->worker_prepare("a", 1));
    assert(player->worker_load_pending());
    assert(!player->worker_prepare("b", 2));
    assert(player->track_instance_id() == 0);
    assert(render().source_frames_rendered == 8);
    assert(output->channels[0][0] == 0.25);
    assert(player->load_status().state == MEDIA_LOAD_COMMITTED);
    assert(player->load_status().request_id == 1 && player->track_instance_id() == 1);
    const auto first_generation = player->generation();
    player->worker_collect();
    assert(!player->worker_load_pending());
    DeckControlEventBlock original_cue{};
    original_cue.events.push_back({.type = DECK_CONTROL_SET_CUE, .value = 16});
    (void)render(&original_cue);
    assert(player->markers().cue_set && player->markers().cue_frame == 16);

    for (const char* path : {"missing", "corrupt", "empty", "partial"}) {
        assert(!player->worker_prepare(path, 2));
        const auto position = player->media_position();
        assert(render().source_frames_rendered == 8);
        assert(player->media_position() == position + 8);
        assert(output->channels[0][0] == 0.25);
        assert(player->generation() == first_generation && player->track_instance_id() == 1);
        assert(player->load_status().state == MEDIA_LOAD_FAILED);
        assert(player->markers().cue_set && player->markers().cue_frame == 16);
        assert(player->load_status().decode_error != DecodeError::None);
        Player::PlaybackDiagnostics diagnostics{};
        assert(player->try_read_diagnostics(diagnostics));
        assert(diagnostics.decode_error == DecodeError::None);
        assert(diagnostics.generation == first_generation && diagnostics.source_sample_rate_hz == 48000);
        static_cast<void>(player->worker_refill());
    }
    original_cue.events.clear();
    original_cue.events.push_back({.type = DECK_CONTROL_RETURN_TO_CUE});
    (void)render(&original_cue);
    assert(player->media_position() == 24);
    assert(player->worker_prepare("b", 3));
    assert(render().source_frames_rendered == 8);
    assert(player->load_status().state == MEDIA_LOAD_PROTECTED);
    assert(player->load_status().request_id == 3 && player->track_instance_id() == 1);
    assert(output->channels[0][0] == 0.25 && player->generation() == first_generation);
    player->worker_collect();

    // Protection is checked at adoption, not at command acceptance/preparation.
    player->set_playing(false);
    assert(player->worker_prepare("b", 4));
    player->set_playing(true);
    (void)render();
    assert(player->load_status().state == MEDIA_LOAD_PROTECTED && player->track_instance_id() == 1);
    player->worker_collect();

    player->set_playing(false);
    assert(player->set_rate_immediate(1.25));
    assert(player->worker_prepare("b", 5));
    DeckControlEventBlock stale_controls{};
    stale_controls.events.push_back({.type = DECK_CONTROL_PLAY});
    stale_controls.events.push_back({.type = DECK_CONTROL_SET_RATE, .value = 2.0});
    (void)render(&stale_controls);
    assert(!player->playing() && player->current_rate() == 1.25);
    assert(player->media_position() == 0 && player->generation() > first_generation);
    assert(player->track_instance_id() == 5 && player->load_status().state == MEDIA_LOAD_COMMITTED);
    assert(player->markers() == DeckMarkerState{});
    player->worker_collect();
    player->set_playing(true);
    (void)render();
    assert(output->channels[0][0] == -0.5);
    const auto second_generation = player->generation();
    assert(player->worker_seek(100));
    static_cast<void>(player->worker_refill());
    (void)render();
    assert(player->generation() > second_generation && player->track_instance_id() == 5);

    // Explicit policy opt-out and repeated reuse of both slots, including renderer reservations.
    for (uint64_t id = 6; id < 56; ++id) {
        const auto generation = player->generation();
        assert(player->worker_prepare(id % 2 == 0 ? "a" : "b", id, false));
        assert(render().source_frames_rendered == 8);
        assert(output->channels[0][0] == (id % 2 == 0 ? 0.25 : -0.5));
        assert(player->generation() > generation && player->track_instance_id() == id);
        assert(player->markers() == DeckMarkerState{});
        player->worker_collect();
        const auto position = player->media_position();
        DeckControlEventBlock cue{};
        cue.events.push_back({.type = DECK_CONTROL_RETURN_TO_CUE});
        (void)render(&cue);
        assert(player->media_position() == position + 8 * player->current_rate());
        cue.events.clear();
        cue.events.push_back({.type = DECK_CONTROL_SET_CUE, .value = 0});
        (void)render(&cue);
    }
    assert(player->worker_prepare("a", 56, false));
    assert(player->worker_cancel_load());
    (void)render();
    assert(player->load_status().state == MEDIA_LOAD_CANCELLED);
    assert(player->track_instance_id() == 55 && output->channels[0][0] == -0.5);
    assert(!player->worker_cancel_load());
    assert(player->worker_close());
    (void)render();
    assert(player->track_instance_id() == 0);
    player->shutdown();

    auto concurrent = std::make_unique<Player>(64, 4);
    concurrent->set_playing(true);
    std::atomic<bool> done{false};
    std::jthread worker{[&] {
        for (uint64_t id = 1001; id <= 1100; ++id) {
            assert(concurrent->worker_prepare(id % 2 == 0 ? "a" : "b", id, false));
            while (concurrent->worker_load_pending()) {
                static_cast<void>(concurrent->worker_refill());
                std::this_thread::yield();
            }
        }
        done.store(true, std::memory_order_release);
    }};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!done.load(std::memory_order_acquire)) {
        assert(std::chrono::steady_clock::now() < deadline);
        rendering = true;
        (void)concurrent->render(48000, 8, 200, 2, *output);
        rendering = false;
        assert(render_allocations == 0);
        if (concurrent->track_instance_id() != 0
            && (output->flags & AUDIO_BLOCK_UNDERRUN) == 0) {
            assert(output->channels[0][0]
                == (concurrent->track_instance_id() % 2 == 0 ? 0.25 : -0.5));
        }
        std::this_thread::yield();
    }
    worker.join();
    assert(concurrent->track_instance_id() == 1100);
    concurrent->shutdown();
}
