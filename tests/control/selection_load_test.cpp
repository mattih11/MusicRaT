#include <musicrat/control/selection_load.hpp>

#include <cassert>

int main() {
    using namespace CommRaT::Messages;
    using Tracker = musicrat::control::SelectionLoad;
    Tracker tracker;
    MediaSelectionResult selection{
        .request_id = 9, .snapshot_id = 5, .entry = {
            .name = "track.wav", .relative_path = "track.wav"},
        .resolved_path = "/media/track.wav",
    };
    LoadMedia command{};
    assert(tracker.begin(selection, command));
    assert(command.path == selection.resolved_path && tracker.selection_id() == 9);
    assert(!tracker.begin(selection, command));
    assert(tracker.accept({.accepted = true, .request_id = 17}));
    assert(tracker.state() == Tracker::State::AwaitingCompletion);
    PlaybackStatusBlock status{};
    status.load_request_id = 16;
    status.load_state = MEDIA_LOAD_COMMITTED;
    status.track_instance_id = 16;
    assert(!tracker.observe(status));
    status.load_request_id = 17;
    status.load_state = MEDIA_LOAD_PREPARED;
    assert(tracker.observe(status) && tracker.busy());
    tracker.outcome_unknown();
    assert(tracker.state() == Tracker::State::OutcomeUnknown);
    assert(!tracker.begin(selection, command));
    status.track_instance_id = 17;
    status.load_state = MEDIA_LOAD_COMMITTED;
    assert(tracker.observe(status) && tracker.state() == Tracker::State::Terminal);
    assert(!tracker.observe(status));
    for (auto terminal : {MEDIA_LOAD_FAILED, MEDIA_LOAD_PROTECTED, MEDIA_LOAD_CANCELLED}) {
        assert(tracker.begin(selection, command));
        assert(tracker.accept({.accepted = true, .request_id = 18}));
        status.load_request_id = 18;
        status.load_state = terminal;
        status.load_decode_error = PLAYBACK_DECODE_ERROR_FILE_OPEN;
        assert(tracker.observe(status) && !tracker.busy());
        assert(tracker.load_state() == terminal && tracker.player_request_id() == 18);
        assert(tracker.load_error() == PLAYBACK_DECODE_ERROR_FILE_OPEN);
    }
    assert(tracker.begin(selection, command));
    assert(tracker.accept({.rejection = MEDIA_LOAD_REJECTION_BUSY}));
    assert(tracker.state() == Tracker::State::Rejected);
    assert(tracker.rejection() == MEDIA_LOAD_REJECTION_BUSY);
    assert(tracker.begin(selection, command));
    assert(!tracker.accept({.accepted = true}));
    assert(tracker.state() == Tracker::State::InvalidReply);
    assert(!tracker.begin(selection, command));
    tracker.reset_for_new_player_session();
    assert(tracker.begin(selection, command));
    assert(!tracker.accept({}));
    assert(tracker.state() == Tracker::State::InvalidReply);
    tracker.reset_for_new_player_session();
    selection.error = MEDIA_BROWSE_ERROR_SELECTION_CHANGED;
    assert(!tracker.begin(selection, command));
    assert(tracker.state() == Tracker::State::ValidationFailed && command.path.empty());
    assert(tracker.browse_error() == MEDIA_BROWSE_ERROR_SELECTION_CHANGED);
    selection.error = MEDIA_BROWSE_ERROR_NONE;
    assert(tracker.begin(selection, command));
    assert(tracker.accept({.accepted = true, .request_id = 19}));
    status.load_request_id = 19;
    status.load_state = MEDIA_LOAD_COMMITTED;
    status.track_instance_id = 18;
    assert(!tracker.observe(status) && tracker.state() == Tracker::State::InvalidReply);
    tracker.reset_for_new_player_session();
    assert(tracker.begin(selection, command));
    tracker.outcome_unknown();
    assert(!tracker.begin(selection, command));
    assert(!tracker.observe(status));
    assert(tracker.accept({.accepted = true, .request_id = 20}));
    assert(tracker.state() == Tracker::State::OutcomeUnknown);
    status.load_request_id = 20;
    status.track_instance_id = 20;
    assert(tracker.observe(status) && tracker.state() == Tracker::State::Terminal);
    assert(tracker.begin(selection, command));
    tracker.unavailable_before_send();
    assert(tracker.state() == Tracker::State::Unavailable && tracker.player_request_id() == 0);
    assert(tracker.begin(selection, command));
    assert(tracker.accept({.accepted = true, .request_id = 21}));
    tracker.unavailable_before_send();
    assert(tracker.state() == Tracker::State::AwaitingCompletion);
}
