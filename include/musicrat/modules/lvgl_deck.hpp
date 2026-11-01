#pragma once

#include <musicrat/backends/lvgl_directory_browser.hpp>
#include <musicrat/backends/lvgl_display_backend.hpp>
#include <musicrat/backends/media/deck_load_worker.hpp>
#include <musicrat/backends/media/waveform_overview.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/lvgl_deck.hpp>

#include <atomic>
#include <bit>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

namespace CommRaT {

class LvglDeck : public MusicRaT::Module2<
    commrat::Output<Messages::DeckControlEventBlock>,
    commrat::SyncedInput<Messages::PlaybackStatusBlock>,
    commrat::Remote<Messages::AudioBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::LvglDeck>> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::DeckControlEventBlock>,
        commrat::SyncedInput<Messages::PlaybackStatusBlock>,
        commrat::Remote<Messages::AudioBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::LvglDeck>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata();
    explicit LvglDeck(const commrat::ModuleConfig& config) : Base(config) {}
    ~LvglDeck() override;

    // One UI-owner producer; also usable by an ordinary-thread headless client.
    [[nodiscard]] bool queue_selection(const Messages::MediaSelectionResult& selection) noexcept;
    [[nodiscard]] bool queue_control(Messages::DeckControlType type) noexcept;
    [[nodiscard]] bool queue_control(Messages::DeckControlType type, double value) noexcept;
    [[nodiscard]] bool queue_seek(const musicrat::backends::media::DeckSeekRequest& request) noexcept;
    [[nodiscard]] musicrat::backends::media::DeckSeekState seek_state() const noexcept {
        return seek_state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint64_t media_frame() const noexcept { return media_frame_.load(std::memory_order_acquire); }
    [[nodiscard]] uint64_t generation() const noexcept { return generation_.load(std::memory_order_acquire); }
    [[nodiscard]] musicrat::backends::media::WaveformState waveform_state() const noexcept {
        return waveform_state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] musicrat::control::SelectionLoad::State load_state() const noexcept {
        return load_state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] Messages::MediaLoadState load_result() const noexcept {
        return load_result_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint64_t active_track() const noexcept {
        return active_track_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint16_t playback_flags() const noexcept {
        return playback_flags_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool connected() const noexcept { return connected_.load(std::memory_order_acquire); }
    [[nodiscard]] uint64_t load_request_id() const noexcept { return load_id_.load(std::memory_order_acquire); }

protected:
    commrat::LifecycleResult on_enable() override;
    void on_disable() override;
    void process(const commrat::Synced<Messages::PlaybackStatusBlock>& status,
                 Messages::DeckControlEventBlock& output) override;

private:
    enum class UiState { Stopped, Starting, Running, Failed };
    void ui_loop(std::stop_token stop);
    void set_ui_state(UiState state) noexcept;
    static std::optional<Messages::LoadMedia::Reply> send_load(void*, const Messages::LoadMedia&);
    static std::optional<Messages::SeekMedia::Reply> send_seek(void*, const Messages::SeekMedia&);
    static bool selected(void*, const Messages::MediaSelectionResult&) noexcept;
    static bool control(void*, Messages::DeckControlType, double) noexcept;
    static bool seek(void*, const musicrat::backends::media::DeckSeekRequest&) noexcept;

    std::unique_ptr<musicrat::backends::media::DeckLoadWorker> loads_{};
    Parameters::LvglDeck ui_params_{};
    musicrat::SpscQueue<Messages::DeckControlEvent, Messages::DeckControlEventBlock::MAX_EVENTS + 1> controls_{};
    std::atomic<bool> cue_release_pending_{false};
    std::atomic<uint64_t> rate_value_bits_{0x3ff0000000000000ULL};
    std::atomic<bool> rate_update_pending_{false};
    uint64_t sequence_{0};
    std::atomic<UiState> ui_state_{UiState::Stopped};
    std::atomic<musicrat::control::SelectionLoad::State> load_state_{
        musicrat::control::SelectionLoad::State::Idle};
    std::atomic<Messages::MediaLoadState> load_result_{Messages::MEDIA_LOAD_IDLE};
    std::atomic<uint64_t> active_track_{0};
    std::atomic<uint64_t> load_id_{0};
    std::atomic<uint16_t> playback_flags_{0};
    std::atomic<bool> connected_{false};
    std::atomic<bool> operation_pending_{false};
    std::atomic<musicrat::backends::media::DeckSeekState> seek_state_{
        musicrat::backends::media::DeckSeekState::Idle};
    std::atomic<uint64_t> media_frame_{0};
    std::atomic<uint64_t> generation_{0};
    std::atomic<musicrat::backends::media::WaveformState> waveform_state_{
        musicrat::backends::media::WaveformState::Unavailable};
    std::jthread ui_thread_{};
};

} // namespace CommRaT
