#include <musicrat/backends/media/deck_load_worker.hpp>

#include <stdexcept>
#include <cstdio>
#include <cmath>
#include <new>
#include <musicrat/dsp/beat_grid.hpp>

namespace musicrat::backends::media {

DeckLoadWorker::DeckLoadWorker(void* context, SendLoad send, uint32_t completion_timeout_ms,
                               SendSeek seek, std::filesystem::path media_root)
    : context_(context), send_(send), seek_(seek), timeout_ms_(completion_timeout_ms),
      media_root_(std::move(media_root)) {
    if (send == nullptr || completion_timeout_ms == 0) {
        throw std::invalid_argument("Deck load worker requires a transport and nonzero timeout");
    }
    thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

bool DeckLoadWorker::submit_seek(const DeckSeekRequest& request) noexcept {
    bool expected = false;
    if (!occupied_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
    if (seeks_.push(request)) return true;
    occupied_.store(false, std::memory_order_release);
    return false;
}

DeckLoadWorker::~DeckLoadWorker() {
    thread_.request_stop();
    thread_.join();
}

bool DeckLoadWorker::submit(const CommRaT::Messages::MediaSelectionResult& selection) noexcept {
    bool expected = false;
    if (!occupied_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
    if (selections_.push(selection)) return true;
    occupied_.store(false, std::memory_order_release);
    return false;
}

void DeckLoadWorker::publish(const CommRaT::Messages::PlaybackStatusBlock& status) noexcept {
    if (!statuses_.push(status)) dropped_.fetch_add(1, std::memory_order_relaxed);
}

void DeckLoadWorker::run(std::stop_token stop) {
    using namespace CommRaT::Messages;
    using State = control::SelectionLoad::State;
    using Clock = std::chrono::steady_clock;
    DeckLoadView view{};
    MediaSelectionResult pending{};
    std::optional<FileIdentity> pending_asset{};
    auto last_status = Clock::time_point{};
    auto started = Clock::time_point{};
    uint64_t named_track = 0;
    bool request_active = false;
    bool seeking = false;
    DeckSeekRequest seek_request{};
    uint64_t seek_sequence = 0;
    while (!stop.stop_requested()) {
        const auto now = Clock::now();
        PlaybackStatusBlock status{};
        while (statuses_.pop(status)) {
            if (last_status != Clock::time_point{}
                && status.sequence_number < view.playback.sequence_number
                && (seeking || view.load.busy() || view.load.state() == State::OutcomeUnknown)) {
                view.session_uncertain = true;
                view.load.outcome_unknown();
            }
            last_status = now;
            view.playback = status;
            if (!view.session_uncertain) static_cast<void>(view.load.observe(status));
            if (seeking && !view.session_uncertain && status.sequence_number > seek_sequence) {
                if (status.track_instance_id != seek_request.track_instance_id
                    || (status.flags & PLAYBACK_STATUS_PLAYING) != 0) {
                    view.seek_state = DeckSeekState::OutcomeUnknown;
                } else if (status.decode_error != PLAYBACK_DECODE_ERROR_NONE) {
                    view.seek_state = DeckSeekState::Failed;
                    seeking = false;
                } else if (status.generation > seek_request.generation
                    && (status.flags & (PLAYBACK_STATUS_INVALID | PLAYBACK_STATUS_SYNC_SEEK_PENDING)) == 0
                    && std::isfinite(status.media_position_frames)
                    && std::abs(status.media_position_frames - static_cast<double>(seek_request.media_frame)) <= 1.0) {
                    view.seek_state = DeckSeekState::PositionObserved;
                    seeking = false;
                }
            }
            if (view.load.state() == State::Terminal
                && view.load.load_state() == MEDIA_LOAD_COMMITTED
                && status.track_instance_id == view.load.player_request_id()
                && named_track != status.track_instance_id) {
                view.active_name = pending.entry.name;
                view.active_path = pending.resolved_path;
                view.active_asset = pending_asset;
                named_track = status.track_instance_id;
                view.rekordbox = {};
                view.rekordbox_track_instance_id = named_track;
                view.rekordbox_import_error = RekordboxImportError::NotFound;
                if (!media_root_.empty() && status.source_sample_rate_hz != 0
                    && status.duration_frames != 0) {
                    try {
                        const auto imported = import_rekordbox_anlz(media_root_,
                            std::filesystem::path{pending.resolved_path.c_str()},
                            status.source_sample_rate_hz, status.duration_frames);
                        view.rekordbox_import_error = imported.error;
                        if (imported.error == RekordboxImportError::None) {
                            view.rekordbox = imported.metadata;
                        }
                    } catch (const std::filesystem::filesystem_error& error) {
                        std::fprintf(stderr, "Rekordbox ANLZ import: %s\n", error.what());
                        view.rekordbox_import_error = RekordboxImportError::Io;
                    } catch (const std::bad_alloc&) {
                        std::fprintf(stderr, "Rekordbox ANLZ import: memory limit reached\n");
                        view.rekordbox_import_error = RekordboxImportError::ResourceLimit;
                    }
                }
            } else if (status.track_instance_id != named_track) {
                view.active_name = {};
                view.active_path = {};
                view.active_asset.reset();
                view.rekordbox = {};
                view.rekordbox_track_instance_id = status.track_instance_id;
                view.rekordbox_import_error = RekordboxImportError::NotFound;
                named_track = status.track_instance_id;
            }
            if (view.rekordbox_track_instance_id == status.track_instance_id
                && view.rekordbox.grid_set) {
                view.playback.beat_grid = view.rekordbox.beat_grid;
                view.playback.beat_grid_track_instance_id = status.track_instance_id;
                const dsp::BeatGridMapper mapper{view.rekordbox.beat_grid};
                if (mapper.valid()) {
                    const auto position = std::max(0.0, status.media_position_frames);
                    view.playback.track_bpm = mapper.tempo_at_frame(position);
                    if (std::isfinite(status.playback_rate) && status.playback_rate > 0) {
                        view.playback.current_bpm = view.playback.track_bpm * status.playback_rate;
                    }
                }
            }
        }
        view.connected = last_status != Clock::time_point{}
            && now - last_status < std::chrono::seconds{1};
        if (!view.connected && (seeking || (view.load.player_request_id() != 0
            && (view.load.busy() || view.load.state() == State::OutcomeUnknown)))) {
            // Player-local IDs cannot safely correlate across a possible process restart.
            view.session_uncertain = true;
        }
        if (seeking && (!view.connected || view.session_uncertain
            || now - started >= std::chrono::milliseconds{timeout_ms_})) {
            view.seek_state = DeckSeekState::OutcomeUnknown;
        }
        if (view.load.busy() && (!view.connected
            || now - started >= std::chrono::milliseconds{timeout_ms_})) {
            view.load.outcome_unknown();
        }
        if (selections_.pop(pending)) {
            request_active = true;
            view.seek_state = DeckSeekState::Idle;
            LoadMedia command{};
            if (view.load.begin(pending, command)) {
                pending_asset = regular_file_identity(command.path.c_str());
                started = now;
                if (!view.connected) {
                    // No command was sent; distinguish this from a lost reply.
                    view.load.unavailable_before_send();
                } else {
                    try {
                        const auto reply = send_(context_, command);
                        if (reply) static_cast<void>(view.load.accept(*reply));
                        else view.load.outcome_unknown();
                    } catch (const std::exception& error) {
                        std::fprintf(stderr, "Deck load RPC: %s\n", error.what());
                        view.load.outcome_unknown();
                    }
                }
            }
        }
        if (seeks_.pop(seek_request)) {
            request_active = true;
            view.seek_target = seek_request.media_frame;
            const auto& current = view.playback;
            if (!seek_ || !view.connected || view.session_uncertain) {
                view.seek_state = DeckSeekState::Unavailable;
            } else if (seek_request.track_instance_id == 0
                || seek_request.track_instance_id != current.track_instance_id
                || seek_request.generation == 0 || seek_request.generation != current.generation
                || current.duration_frames == 0 || current.decode_error != PLAYBACK_DECODE_ERROR_NONE
                || seek_request.media_frame >= current.duration_frames
                || current.source_sample_rate_hz == 0
                || current.transport_sync_mode != TRANSPORT_SYNC_OFF
                || (current.flags & (PLAYBACK_STATUS_PLAYING | PLAYBACK_STATUS_INVALID
                    | PLAYBACK_STATUS_LOOPING | PLAYBACK_STATUS_SYNC_SEEK_PENDING)) != 0) {
                view.seek_state = DeckSeekState::InvalidTarget;
            } else {
                seeking = true;
                started = now;
                seek_sequence = current.sequence_number;
                try {
                    const auto reply = seek_(context_, {.media_frame = seek_request.media_frame});
                    view.seek_state = !reply ? DeckSeekState::OutcomeUnknown
                        : reply->accepted ? DeckSeekState::AwaitingPosition : DeckSeekState::Rejected;
                    if (reply && !reply->accepted) seeking = false;
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "Deck seek RPC: %s\n", error.what());
                    view.seek_state = DeckSeekState::OutcomeUnknown;
                }
            }
        }
        const auto state = view.load.state();
        if (request_active && !seeking && !view.load.busy()
            && state != State::OutcomeUnknown && state != State::InvalidReply) {
            request_active = false;
            occupied_.store(false, std::memory_order_release);
        }
        view.operation_pending = occupied_.load(std::memory_order_acquire);
        view.dropped_status = dropped_.load(std::memory_order_relaxed);
        // A full UI queue retains current state here for a subsequent publication.
        static_cast<void>(views_.push(view));
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
}

} // namespace musicrat::backends::media
