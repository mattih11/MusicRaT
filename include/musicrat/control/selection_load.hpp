#pragma once

#include <musicrat/protocol/file_player.hpp>
#include <musicrat/protocol/media_library.hpp>
#include <musicrat/protocol/playback_status.hpp>

namespace musicrat::control {

// Single in-band owner correlates browser intent, RPC acceptance, then authoritative status.
class SelectionLoad {
public:
    enum class State { Idle, AwaitingAcceptance, AwaitingCompletion, Terminal, Rejected,
                       ValidationFailed, InvalidReply, OutcomeUnknown, Unavailable };

    [[nodiscard]] bool begin(
        const CommRaT::Messages::MediaSelectionResult& selection,
        CommRaT::Messages::LoadMedia& command) noexcept {
        using namespace CommRaT::Messages;
        if (busy() || state_ == State::OutcomeUnknown || state_ == State::InvalidReply) return false;
        command = {};
        selection_id_ = selection.request_id;
        snapshot_id_ = selection.snapshot_id;
        player_request_id_ = 0;
        load_state_ = MEDIA_LOAD_IDLE;
        load_error_ = PLAYBACK_DECODE_ERROR_NONE;
        rejection_ = MEDIA_LOAD_REJECTION_NONE;
        browse_error_ = selection.error;
        if (selection.error != MEDIA_BROWSE_ERROR_NONE || selection.request_id == 0
            || selection.snapshot_id == 0 || selection.entry.kind != MEDIA_DIRECTORY_ENTRY_FILE
            || selection.entry.relative_path.empty() || selection.resolved_path.empty()
            || contains_nul(selection.resolved_path)) {
            if (browse_error_ == MEDIA_BROWSE_ERROR_NONE) browse_error_ = MEDIA_BROWSE_ERROR_INVALID_REQUEST;
            state_ = State::ValidationFailed;
            return false;
        }
        command.path = selection.resolved_path;
        state_ = State::AwaitingAcceptance;
        return true;
    }

    [[nodiscard]] bool accept(const CommRaT::Messages::LoadMedia::Reply& reply) noexcept {
        using namespace CommRaT::Messages;
        const bool uncertain = state_ == State::OutcomeUnknown && player_request_id_ == 0;
        if (state_ != State::AwaitingAcceptance && !uncertain) return false;
        if (reply.accepted) {
            if (reply.request_id == 0 || reply.rejection != MEDIA_LOAD_REJECTION_NONE) {
                state_ = State::InvalidReply;
                return false;
            }
            player_request_id_ = reply.request_id;
            if (!uncertain) state_ = State::AwaitingCompletion;
        } else {
            if (reply.request_id != 0 || reply.rejection == MEDIA_LOAD_REJECTION_NONE
                || reply.rejection > MEDIA_LOAD_REJECTION_ID_EXHAUSTED) {
                state_ = State::InvalidReply;
                return false;
            }
            rejection_ = reply.rejection;
            state_ = State::Rejected;
        }
        return true;
    }

    [[nodiscard]] bool observe(const CommRaT::Messages::PlaybackStatusBlock& status) noexcept {
        using namespace CommRaT::Messages;
        if (state_ != State::AwaitingCompletion && state_ != State::OutcomeUnknown) return false;
        if (player_request_id_ == 0 || status.load_request_id != player_request_id_) return false;
        if (status.load_state > MEDIA_LOAD_CANCELLED
            || status.load_decode_error > PLAYBACK_DECODE_ERROR_POOL_FAILURE
            || status.load_state == MEDIA_LOAD_IDLE
            || (status.load_state == MEDIA_LOAD_COMMITTED && status.track_instance_id != player_request_id_)) {
            state_ = State::InvalidReply;
            return false;
        }
        load_state_ = status.load_state;
        load_error_ = status.load_decode_error;
        if (load_state_ >= MEDIA_LOAD_COMMITTED) state_ = State::Terminal;
        return true;
    }

    // Transport timeout/disconnection is not evidence that the player cancelled the load.
    void outcome_unknown() noexcept {
        if (busy()) state_ = State::OutcomeUnknown;
    }

    void reset_for_new_player_session() noexcept { *this = {}; }

    void unavailable_before_send() noexcept {
        if (state_ == State::AwaitingAcceptance) state_ = State::Unavailable;
    }

    [[nodiscard]] bool busy() const noexcept {
        return state_ == State::AwaitingAcceptance || state_ == State::AwaitingCompletion;
    }
    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] uint64_t selection_id() const noexcept { return selection_id_; }
    [[nodiscard]] uint64_t snapshot_id() const noexcept { return snapshot_id_; }
    [[nodiscard]] uint64_t player_request_id() const noexcept { return player_request_id_; }
    [[nodiscard]] CommRaT::Messages::MediaLoadState load_state() const noexcept { return load_state_; }
    [[nodiscard]] CommRaT::Messages::PlaybackDecodeError load_error() const noexcept { return load_error_; }
    [[nodiscard]] CommRaT::Messages::MediaLoadRejection rejection() const noexcept { return rejection_; }
    [[nodiscard]] CommRaT::Messages::MediaBrowseError browse_error() const noexcept { return browse_error_; }

private:
    static bool contains_nul(const sertial::fixed_string<512>& path) noexcept {
        for (std::size_t index = 0; index < path.size(); ++index) {
            if (path[index] == '\0') return true;
        }
        return false;
    }

    State state_{State::Idle};
    uint64_t selection_id_{0};
    uint64_t snapshot_id_{0};
    uint64_t player_request_id_{0};
    CommRaT::Messages::MediaLoadState load_state_{CommRaT::Messages::MEDIA_LOAD_IDLE};
    CommRaT::Messages::PlaybackDecodeError load_error_{CommRaT::Messages::PLAYBACK_DECODE_ERROR_NONE};
    CommRaT::Messages::MediaLoadRejection rejection_{CommRaT::Messages::MEDIA_LOAD_REJECTION_NONE};
    CommRaT::Messages::MediaBrowseError browse_error_{CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE};
};

} // namespace musicrat::control
