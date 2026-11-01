#include <musicrat/backends/media/deck_load_worker.hpp>

#include <cassert>
#include <chrono>
#include <thread>
#include <stdexcept>

namespace {
using namespace CommRaT::Messages;
using musicrat::backends::media::DeckLoadView;
using musicrat::backends::media::DeckLoadWorker;
using State = musicrat::control::SelectionLoad::State;

struct Transport {
    std::optional<LoadMedia::Reply> reply{LoadMedia::Reply{.accepted = true, .request_id = 7}};
    std::atomic<uint32_t> calls{0};
    bool fail{false};
    std::optional<SeekMedia::Reply> seek_reply{SeekMedia::Reply{.accepted = true}};
    std::atomic<uint32_t> seek_calls{0};
    static std::optional<SeekMedia::Reply> seek(void* context, const SeekMedia& command) {
        auto& self = *static_cast<Transport*>(context);
        assert(command.media_frame == 24000);
        self.seek_calls.fetch_add(1);
        if (self.fail) throw std::runtime_error("test seek failure");
        return self.seek_reply;
    }
    static std::optional<LoadMedia::Reply> send(void* context, const LoadMedia& command) {
        auto& self = *static_cast<Transport*>(context);
        assert(command.path == "/media/song.wav");
        self.calls.fetch_add(1);
        if (self.fail) throw std::runtime_error("test transport failure");
        return self.reply;
    }
};

MediaSelectionResult selection() {
    return {.request_id = 4, .snapshot_id = 3,
        .entry = {.name = "song.wav", .relative_path = "song.wav"},
        .resolved_path = "/media/song.wav"};
}

template<typename Predicate>
DeckLoadView await(DeckLoadWorker& worker, Predicate predicate, const PlaybackStatusBlock* status = nullptr) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    DeckLoadView view{};
    while (std::chrono::steady_clock::now() < deadline) {
        if (status) worker.publish(*status);
        while (worker.poll(view)) {
            if (predicate(view)) return view;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    assert(false && "deck worker deadline");
    return {};
}
} // namespace

int main() {
    PlaybackStatusBlock status{};
    {
        Transport transport;
        DeckLoadWorker worker{&transport, Transport::send};
        assert(worker.submit(selection()));
        auto view = await(worker, [](const auto& value) { return value.load.state() == State::Unavailable; });
        assert(!view.connected && transport.calls.load() == 0);
        await(worker, [](const auto& value) { return value.connected; }, &status);
        assert(worker.submit(selection()));
        assert(!worker.submit(selection()));
        await(worker, [](const auto& value) { return value.load.state() == State::AwaitingCompletion; }, &status);
        status.load_request_id = 6;
        status.load_state = MEDIA_LOAD_COMMITTED;
        status.track_instance_id = 6;
        for (int index = 0; index < 10; ++index) {
            worker.publish(status);
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        assert(!worker.submit(selection()));
        status.load_request_id = 7;
        status.track_instance_id = 7;
        view = await(worker, [](const auto& value) { return value.load.state() == State::Terminal; }, &status);
        assert(view.active_name == "song.wav" && view.load.player_request_id() == 7);
        assert(transport.calls.load() == 1);
        view = await(worker, [](const auto& value) { return !value.connected; });
        assert(view.playback.track_instance_id == 7);
    }
    status = {};
    {
        Transport transport;
        transport.reply = LoadMedia::Reply{.rejection = MEDIA_LOAD_REJECTION_BUSY};
        DeckLoadWorker worker{&transport, Transport::send};
        await(worker, [](const auto& value) { return value.connected; }, &status);
        assert(worker.submit(selection()));
        const auto view = await(worker, [](const auto& value) { return value.load.state() == State::Rejected; }, &status);
        assert(view.load.rejection() == MEDIA_LOAD_REJECTION_BUSY);
    }
    {
        Transport transport;
        transport.reply = std::nullopt;
        DeckLoadWorker worker{&transport, Transport::send};
        await(worker, [](const auto& value) { return value.connected; }, &status);
        assert(worker.submit(selection()));
        await(worker, [](const auto& value) { return value.load.state() == State::OutcomeUnknown; }, &status);
        assert(!worker.submit(selection()) && transport.calls.load() == 1);
    }
    {
        Transport transport;
        transport.fail = true;
        DeckLoadWorker worker{&transport, Transport::send};
        await(worker, [](const auto& value) { return value.connected; }, &status);
        assert(worker.submit(selection()));
        await(worker, [](const auto& value) { return value.load.state() == State::OutcomeUnknown; }, &status);
        assert(!worker.submit(selection()));
    }
    status = {};
    {
        Transport transport;
        DeckLoadWorker worker{&transport, Transport::send};
        status.sequence_number = 100;
        await(worker, [](const auto& value) { return value.connected; }, &status);
        assert(worker.submit(selection()));
        await(worker, [](const auto& value) { return value.load.state() == State::AwaitingCompletion; }, &status);
        status.sequence_number = 1;
        status.load_request_id = 7;
        status.track_instance_id = 7;
        status.load_state = MEDIA_LOAD_COMMITTED;
        const auto view = await(worker, [](const auto& value) { return value.session_uncertain; }, &status);
        assert(view.load.state() == State::OutcomeUnknown && !worker.submit(selection()));
    }
    status = {};
    {
        Transport transport;
        transport.reply = LoadMedia::Reply{.accepted = true};
        DeckLoadWorker worker{&transport, Transport::send};
        await(worker, [](const auto& value) { return value.connected; }, &status);
        assert(worker.submit(selection()));
        await(worker, [](const auto& value) { return value.load.state() == State::InvalidReply; }, &status);
        assert(!worker.submit(selection()));
    }
    {
        Transport transport;
        DeckLoadWorker worker{&transport, Transport::send, 50};
        await(worker, [](const auto& value) { return value.connected; }, &status);
        assert(worker.submit(selection()));
        await(worker, [](const auto& value) { return value.load.state() == State::OutcomeUnknown; }, &status);
        assert(!worker.submit(selection()));
        status.load_request_id = 7;
        status.load_state = MEDIA_LOAD_PROTECTED;
        const auto view = await(worker, [](const auto& value) { return value.load.state() == State::Terminal; }, &status);
        assert(view.load.load_state() == MEDIA_LOAD_PROTECTED && view.active_name.empty());
    }
    status = {};
    {
        Transport transport;
        DeckLoadWorker worker{&transport, Transport::send};
        await(worker, [](const auto& value) { return value.connected; }, &status);
        assert(worker.submit(selection()));
        await(worker, [](const auto& value) { return value.load.state() == State::AwaitingCompletion; }, &status);
        await(worker, [](const auto& value) { return value.session_uncertain && !value.connected; });
        status.load_request_id = 7;
        status.track_instance_id = 7;
        status.load_state = MEDIA_LOAD_COMMITTED;
        const auto view = await(worker, [](const auto& value) { return value.connected; }, &status);
        assert(view.session_uncertain && view.load.state() == State::OutcomeUnknown);
        assert(!worker.submit(selection()));
    }
    {
        Transport transport;
        DeckLoadWorker worker{&transport, Transport::send};
        for (int index = 0; index < 10000; ++index) worker.publish(status);
        const auto view = await(worker, [](const auto& value) { return value.dropped_status > 0; });
        assert(view.dropped_status > 0);
    }
    musicrat::SpscQueue<uint32_t, 3> queue;
    assert(queue.push(1) && queue.push(2) && !queue.push(3));
    uint32_t value{};
    assert(queue.pop(value) && value == 1);
    assert(queue.push(3));
    assert(queue.pop(value) && value == 2);
    assert(queue.pop(value) && value == 3 && !queue.pop(value));

    using SeekState = musicrat::backends::media::DeckSeekState;
    const musicrat::backends::media::DeckSeekRequest request{
        .track_instance_id = 9, .generation = 2, .media_frame = 24000};
    auto paused = [] {
        PlaybackStatusBlock status{};
        status.track_instance_id = 9;
        status.generation = 2;
        status.source_sample_rate_hz = 48000;
        status.duration_frames = 48000;
        status.sequence_number = 10;
        return status;
    };
    for (int mode = 0; mode < 9; ++mode) {
        auto current = paused();
        if (mode == 0) current.flags = PLAYBACK_STATUS_PLAYING;
        if (mode == 1) current.flags = PLAYBACK_STATUS_LOOPING;
        if (mode == 2) current.track_instance_id = 10;
        if (mode == 3) current.generation = 3;
        if (mode == 4) current.duration_frames = 24000;
        if (mode == 5) current.transport_sync_mode = TRANSPORT_SYNC_BEAT;
        if (mode == 6) current.decode_error = PLAYBACK_DECODE_ERROR_SEEK_FAILED;
        if (mode == 7) current.duration_frames = 0;
        if (mode == 8) current.source_sample_rate_hz = 0;
        Transport transport;
        DeckLoadWorker worker{&transport, Transport::send, 1000, Transport::seek};
        await(worker, [](const auto& value) { return value.connected; }, &current);
        assert(worker.submit_seek(request));
        auto view = await(worker, [](const auto& value) { return value.seek_state == SeekState::InvalidTarget; }, &current);
        assert(!view.operation_pending && transport.seek_calls.load() == 0);
    }
    for (int mode = 0; mode < 5; ++mode) {
        auto current = paused();
        Transport transport;
        if (mode == 1) transport.seek_reply = SeekMedia::Reply{.accepted = false};
        if (mode == 2) transport.seek_reply = std::nullopt;
        if (mode == 3) transport.fail = true;
        DeckLoadWorker worker{&transport, Transport::send, 50, Transport::seek};
        await(worker, [](const auto& value) { return value.connected; }, &current);
        assert(worker.submit_seek(request));
        assert(!worker.submit(selection()) && !worker.submit_seek(request));
        if (mode == 1) {
            const auto view = await(worker, [](const auto& value) { return value.seek_state == SeekState::Rejected; }, &current);
            assert(!view.operation_pending);
            continue;
        }
        await(worker, [mode](const auto& value) {
            return value.seek_state == (mode == 0 || mode == 4 ? SeekState::AwaitingPosition : SeekState::OutcomeUnknown);
        }, &current);
        if (mode == 4) {
            ++current.sequence_number;
            current.decode_error = PLAYBACK_DECODE_ERROR_SEEK_FAILED;
            const auto view = await(worker, [](const auto& value) { return value.seek_state == SeekState::Failed; }, &current);
            assert(!view.operation_pending);
            continue;
        }
        // An unchanged generation at the desired position is not seek completion.
        current.media_position_frames = 24000;
        ++current.sequence_number;
        await(worker, [](const auto& value) { return value.seek_state == SeekState::OutcomeUnknown; }, &current);
        assert(!worker.submit(selection()));
        ++current.generation;
        ++current.sequence_number;
        const auto view = await(worker, [](const auto& value) { return value.seek_state == SeekState::PositionObserved; }, &current);
        assert(!view.operation_pending && transport.seek_calls.load() == 1);
    }
    {
        Transport transport;
        DeckLoadWorker worker{&transport, Transport::send, 1000, Transport::seek};
        assert(worker.submit_seek(request));
        auto view = await(worker, [](const auto& value) { return value.seek_state == SeekState::Unavailable; });
        assert(!view.operation_pending && transport.seek_calls.load() == 0);
    }
    {
        auto current = paused();
        Transport transport;
        DeckLoadWorker worker{&transport, Transport::send, 1000, Transport::seek};
        await(worker, [](const auto& value) { return value.connected; }, &current);
        assert(worker.submit_seek(request));
        await(worker, [](const auto& value) { return value.seek_state == SeekState::AwaitingPosition; }, &current);
        await(worker, [](const auto& value) { return value.session_uncertain; });
        ++current.sequence_number;
        ++current.generation;
        current.media_position_frames = 24000;
        const auto view = await(worker, [](const auto& value) { return value.connected; }, &current);
        assert(view.seek_state == SeekState::OutcomeUnknown && view.operation_pending);
        assert(!worker.submit(selection()));
    }
    {
        auto current = paused();
        current.sequence_number = 100;
        Transport transport;
        DeckLoadWorker worker{&transport, Transport::send, 1000, Transport::seek};
        await(worker, [](const auto& value) { return value.connected; }, &current);
        assert(worker.submit_seek(request));
        await(worker, [](const auto& value) { return value.seek_state == SeekState::AwaitingPosition; }, &current);
        current.sequence_number = 1;
        ++current.generation;
        current.media_position_frames = 24000;
        const auto view = await(worker, [](const auto& value) { return value.session_uncertain; }, &current);
        assert(view.seek_state == SeekState::OutcomeUnknown && view.operation_pending);
        assert(!worker.submit_seek(request));
    }
}
