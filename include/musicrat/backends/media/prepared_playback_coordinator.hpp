#pragma once

#include <musicrat/backends/media/playback_coordinator.hpp>
#include <musicrat/protocol/media_load.hpp>

#include <array>
#include <atomic>
#include <cstdint>

namespace musicrat::backends::media {

// One worker owns decoder operations; one render thread owns both renderers.
template<DecoderBackend Decoder, std::size_t ChunkCount>
class PreparedPlaybackCoordinator {
    static_assert(std::atomic<uint8_t>::is_always_lock_free);
    using Coordinator = PlaybackCoordinator<Decoder, ChunkCount>;
    enum class Handoff : uint32_t { Idle, Prepared, Adopting, Committed, Protected };

public:
    using AudioBlock = typename Coordinator::AudioBlock;
    using PlaybackDiagnostics = typename Coordinator::PlaybackDiagnostics;

    struct LoadStatus {
        uint64_t request_id{0};
        CommRaT::Messages::MediaLoadState state{CommRaT::Messages::MEDIA_LOAD_IDLE};
        DecodeError decode_error{DecodeError::None};
    };

    explicit PreparedPlaybackCoordinator(
        uint32_t chunk_frames = AudioBlock::MAX_FRAMES,
        std::size_t high_watermark = ChunkCount,
        double output_sample_rate_hz = 0.0,
        bool pitch_lock = false)
        : slots_{Coordinator{chunk_frames, high_watermark, output_sample_rate_hz, pitch_lock},
                 Coordinator{chunk_frames, high_watermark, output_sample_rate_hz, pitch_lock}} {}

    // Returns false without disturbing the current track when candidate preparation fails.
    // Calls must be serialized with other worker operations, including result collection.
    [[nodiscard]] bool worker_prepare(
        const char* path, uint64_t request_id, bool protect_playing = true) noexcept {
        if (worker_load_pending() || request_id == 0) return false;
        publish_load({request_id, CommRaT::Messages::MEDIA_LOAD_PREPARING});
        auto& candidate = slots_[1 - worker_slot_];
        const bool opened = candidate.worker_open(path);
        const auto refill = opened ? candidate.worker_refill() : DecodeAheadRefillResult{};
        PlaybackDiagnostics diagnostics{};
        const bool valid = candidate.try_read_diagnostics(diagnostics);
        if (!opened || !valid || diagnostics.decode_error != DecodeError::None
            || refill.state == DecodeAheadState::Error || diagnostics.resident_frames == 0) {
            const auto error = valid && diagnostics.decode_error != DecodeError::None
                ? diagnostics.decode_error : DecodeError::InvalidMetadata;
            candidate.worker_close();
            publish_load({request_id, CommRaT::Messages::MEDIA_LOAD_FAILED, error});
            return false;
        }
        prepared_request_id_ = request_id;
        protect_playing_ = protect_playing;
        publish_load({request_id, CommRaT::Messages::MEDIA_LOAD_PREPARED});
        handoff_.store(Handoff::Prepared, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool worker_load_pending() const noexcept {
        return handoff_.load(std::memory_order_acquire) != Handoff::Idle;
    }

    [[nodiscard]] bool worker_cancel_load() noexcept {
        Handoff expected = Handoff::Prepared;
        if (!handoff_.compare_exchange_strong(expected, Handoff::Idle,
                std::memory_order_acq_rel, std::memory_order_acquire)) return false;
        slots_[1 - worker_slot_].worker_close();
        publish_load({prepared_request_id_, CommRaT::Messages::MEDIA_LOAD_CANCELLED});
        return true;
    }

    void worker_cancel_request(uint64_t request_id) noexcept {
        publish_load({request_id, CommRaT::Messages::MEDIA_LOAD_CANCELLED});
    }

    // The render acknowledgement retires old storage; worker cleanup never races its use.
    void worker_collect() noexcept {
        const auto state = handoff_.load(std::memory_order_acquire);
        if (state == Handoff::Committed) {
            slots_[worker_slot_].worker_close();
            worker_slot_ = 1 - worker_slot_;
            publish_load({prepared_request_id_, CommRaT::Messages::MEDIA_LOAD_COMMITTED});
        } else if (state == Handoff::Protected) {
            slots_[1 - worker_slot_].worker_close();
            publish_load({prepared_request_id_, CommRaT::Messages::MEDIA_LOAD_PROTECTED});
        } else {
            return;
        }
        handoff_.store(Handoff::Idle, std::memory_order_release);
    }

    [[nodiscard]] DecodeAheadRefillResult worker_refill() noexcept {
        worker_collect();
        return slots_[worker_slot_].worker_refill();
    }

    [[nodiscard]] bool worker_seek(uint64_t media_frame) noexcept {
        return !worker_load_pending() && slots_[worker_slot_].worker_seek(media_frame);
    }

    [[nodiscard]] bool worker_close() noexcept {
        // Lifecycle callers defer unload until the pending handoff is acknowledged.
        if (worker_load_pending()) return false;
        slots_[worker_slot_].worker_close();
        return true;
    }

    // Only after both worker and render execution have stopped.
    void shutdown() noexcept {
        worker_collect();
        static_cast<void>(worker_cancel_load());
        slots_[0].worker_close();
        slots_[1].worker_close();
        handoff_.store(Handoff::Idle, std::memory_order_relaxed);
    }

    [[nodiscard]] bool set_rate_immediate(double rate) noexcept { return active().set_rate_immediate(rate); }
    [[nodiscard]] bool ramp_rate(double rate, uint32_t frames) noexcept { return active().ramp_rate(rate, frames); }
    void set_playing(bool playing) noexcept { active().set_playing(playing); }
    [[nodiscard]] double media_position() const noexcept { return active().media_position(); }
    [[nodiscard]] double current_rate() const noexcept { return active().current_rate(); }
    [[nodiscard]] uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] uint64_t track_instance_id() const noexcept { return track_instance_id_; }
    [[nodiscard]] bool playing() const noexcept { return active().playing(); }
    [[nodiscard]] bool loop_enabled() const noexcept { return active().loop_enabled(); }
    [[nodiscard]] CommRaT::Messages::DeckMarkerState markers() const noexcept { return active().markers(); }
    [[nodiscard]] bool take_seek_request(uint64_t& media_frame) noexcept { return active().take_seek_request(media_frame); }
    [[nodiscard]] bool pitch_lock_enabled() const noexcept { return active().pitch_lock_enabled(); }
    [[nodiscard]] uint32_t algorithmic_latency_frames() const noexcept { return active().algorithmic_latency_frames(); }
    [[nodiscard]] double minimum_rate() const noexcept { return active().minimum_rate(); }
    [[nodiscard]] double maximum_rate() const noexcept { return active().maximum_rate(); }
    [[nodiscard]] const LoadStatus& load_status() const noexcept { return audio_load_status_; }

    musicrat::dsp::VarispeedRenderResult render(
        double output_rate, uint32_t frames, uint64_t timestamp, uint64_t sequence,
        AudioBlock& output,
        const CommRaT::Messages::DeckControlEventBlock* controls = nullptr) noexcept {
        LoadStatus published{};
        if (read_load(published) && published.request_id != terminal_load_status_.request_id) {
            audio_load_status_ = published;
        }
        Handoff acknowledgement = Handoff::Idle;
        bool replaced = false;
        Handoff expected = Handoff::Prepared;
        if (handoff_.compare_exchange_strong(expected, Handoff::Adopting,
                std::memory_order_acquire, std::memory_order_relaxed)) {
            if (protect_playing_ && track_instance_id_ != 0 && active().playing()) {
                terminal_load_status_ = {prepared_request_id_, CommRaT::Messages::MEDIA_LOAD_PROTECTED};
                acknowledgement = Handoff::Protected;
            } else {
                const auto rate = active().current_rate();
                const bool was_playing = active().playing();
                active().retire_renderer();
                audio_slot_ = 1 - audio_slot_;
                active().clear_track_controls();
                static_cast<void>(active().set_rate_immediate(rate));
                active().set_playing(was_playing);
                track_instance_id_ = prepared_request_id_;
                terminal_load_status_ = {prepared_request_id_, CommRaT::Messages::MEDIA_LOAD_COMMITTED};
                acknowledgement = Handoff::Committed;
                replaced = true;
            }
            audio_load_status_ = terminal_load_status_;
        }
        const auto result = active().render(output_rate, frames, timestamp, sequence, output,
            replaced ? nullptr : controls);
        if (replaced || adopted_inner_generation_ != active().generation()) {
            adopted_inner_generation_ = active().generation();
            ++generation_;
            PlaybackDiagnostics diagnostics{};
            if (active().try_read_diagnostics(diagnostics)
                && diagnostics.worker_state == DecodeAheadState::Closed) {
                track_instance_id_ = 0;
            }
        }
        if (acknowledgement != Handoff::Idle) {
            handoff_.store(acknowledgement, std::memory_order_release);
        }
        return result;
    }

    [[nodiscard]] bool try_read_diagnostics(PlaybackDiagnostics& diagnostics) const noexcept {
        if (!active().try_read_diagnostics(diagnostics)
            || diagnostics.generation != adopted_inner_generation_) return false;
        diagnostics.generation = generation_;
        return true;
    }

private:
    Coordinator& active() noexcept { return slots_[audio_slot_]; }
    const Coordinator& active() const noexcept { return slots_[audio_slot_]; }

    void publish_load(const LoadStatus& status) noexcept {
        load_version_.fetch_add(1, std::memory_order_acq_rel);
        load_request_id_.store(status.request_id, std::memory_order_relaxed);
        load_state_.store(status.state, std::memory_order_relaxed);
        load_error_.store(static_cast<uint32_t>(status.decode_error), std::memory_order_relaxed);
        load_version_.fetch_add(1, std::memory_order_release);
    }

    [[nodiscard]] bool read_load(LoadStatus& status) const noexcept {
        for (uint32_t attempt = 0; attempt < 2; ++attempt) {
            const auto before = load_version_.load(std::memory_order_acquire);
            if ((before & 1U) != 0) return false;
            status.request_id = load_request_id_.load(std::memory_order_relaxed);
            status.state = load_state_.load(std::memory_order_relaxed);
            status.decode_error = static_cast<DecodeError>(load_error_.load(std::memory_order_relaxed));
            if (before == load_version_.load(std::memory_order_acquire)) return true;
        }
        return false;
    }

    std::array<Coordinator, 2> slots_;
    uint32_t worker_slot_{0};
    uint32_t audio_slot_{0};
    std::atomic<Handoff> handoff_{Handoff::Idle};
    uint64_t prepared_request_id_{0};
    bool protect_playing_{true};
    uint64_t adopted_inner_generation_{0};
    uint64_t generation_{0};
    uint64_t track_instance_id_{0};
    LoadStatus audio_load_status_{};
    LoadStatus terminal_load_status_{};
    std::atomic<uint64_t> load_version_{0};
    std::atomic<uint64_t> load_request_id_{0};
    std::atomic<CommRaT::Messages::MediaLoadState> load_state_{CommRaT::Messages::MEDIA_LOAD_IDLE};
    std::atomic<uint32_t> load_error_{0};
};

} // namespace musicrat::backends::media
