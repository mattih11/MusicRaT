#pragma once

#include <musicrat/control/selection_load.hpp>
#include <musicrat/backends/media/file_identity.hpp>
#include <musicrat/backends/media/rekordbox_anlz.hpp>
#include <musicrat/utility/spsc_queue.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>

namespace musicrat::backends::media {

enum class DeckSeekState { Idle, AwaitingPosition, PositionObserved, Rejected,
                           Unavailable, InvalidTarget, Failed, OutcomeUnknown };

struct DeckSeekRequest {
    uint64_t track_instance_id{0};
    uint64_t generation{0};
    uint64_t media_frame{0};
};

struct DeckLoadView {
    control::SelectionLoad load{};
    CommRaT::Messages::PlaybackStatusBlock playback{};
    RekordboxTrackMetadata rekordbox{};
    uint64_t rekordbox_track_instance_id{0};
    RekordboxImportError rekordbox_import_error{RekordboxImportError::NotFound};
    sertial::fixed_string<255> active_name{};
    sertial::fixed_string<512> active_path{};
    std::optional<FileIdentity> active_asset{};
    bool connected{false};
    bool session_uncertain{false};
    uint64_t dropped_status{0};
    DeckSeekState seek_state{DeckSeekState::Idle};
    uint64_t seek_target{0};
    bool operation_pending{false};
};

// RPC, timeout clocks and state reconciliation are owned by an ordinary thread.
class DeckLoadWorker {
    static_assert(std::atomic<uint64_t>::is_always_lock_free);

public:
    using SendLoad = std::optional<CommRaT::Messages::LoadMedia::Reply> (*)(
        void*, const CommRaT::Messages::LoadMedia&);
    using SendSeek = std::optional<CommRaT::Messages::SeekMedia::Reply> (*)(
        void*, const CommRaT::Messages::SeekMedia&);

    DeckLoadWorker(void* context, SendLoad send, uint32_t completion_timeout_ms = 10000,
                   SendSeek seek = nullptr, std::filesystem::path media_root = {});
    ~DeckLoadWorker();

    // UI owner submits and polls. CommRaT's process thread only publishes status.
    [[nodiscard]] bool submit(const CommRaT::Messages::MediaSelectionResult& selection) noexcept;
    [[nodiscard]] bool submit_seek(const DeckSeekRequest& request) noexcept;
    [[nodiscard]] bool poll(DeckLoadView& view) noexcept { return views_.pop(view); }
    void publish(const CommRaT::Messages::PlaybackStatusBlock& status) noexcept;

private:
    void run(std::stop_token stop);

    void* context_;
    SendLoad send_;
    SendSeek seek_;
    uint32_t timeout_ms_;
    std::filesystem::path media_root_;
    SpscQueue<CommRaT::Messages::MediaSelectionResult, 2> selections_{};
    SpscQueue<DeckSeekRequest, 2> seeks_{};
    SpscQueue<CommRaT::Messages::PlaybackStatusBlock, 128> statuses_{};
    SpscQueue<DeckLoadView, 4> views_{};
    std::atomic<bool> occupied_{false};
    std::atomic<uint64_t> dropped_{0};
    std::jthread thread_;
};

} // namespace musicrat::backends::media
