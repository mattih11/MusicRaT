#pragma once

#include <musicrat/backends/audio/audio_output_backend.hpp>

#include "queue_drift_controller.hpp"

#include <cstdint>

namespace musicrat::backends::audio::detail {

class PlaybackDiagnosticRecorder {
public:
    void reset(AudioOutputDiagnostics* capture) noexcept {
        capture_ = capture;
        previous_timestamp_ns_ = 0;
        next_snapshot_ns_ = 0;
        previous_limit_ = 0;
    }

    [[nodiscard]] bool enabled() const noexcept {
        return capture_ != nullptr;
    }

    [[nodiscard]] bool select(AudioOutputDiagnosticEvent& event) noexcept {
        if (!enabled()) {
            return false;
        }
        event.worker_gap_ns = previous_timestamp_ns_ == 0
            ? 0 : event.timestamp_ns - previous_timestamp_ns_;
        previous_timestamp_ns_ = event.timestamp_ns;
        if (event.timestamp_ns >= next_snapshot_ns_) {
            event.reasons |= diagnostic_snapshot;
            next_snapshot_ns_ = event.timestamp_ns + 1'000'000'000ULL;
        }
        if (event.worker_gap_ns >= capture_->slow_operation_threshold_ns) {
            event.reasons |= diagnostic_worker_gap;
        }
        if (event.write_duration_ns >= capture_->slow_operation_threshold_ns) {
            event.reasons |= diagnostic_slow_write;
        }
        if (event.wait_duration_ns >= capture_->slow_operation_threshold_ns) {
            event.reasons |= diagnostic_slow_wait;
        }
        constexpr int32_t maximum = QueueDriftController::maximum_correction_ppm;
        const int32_t limit = event.correction_ppm >= maximum ? 1
            : event.correction_ppm <= -maximum ? -1 : 0;
        if (limit != previous_limit_) {
            event.reasons |= diagnostic_correction_limit;
            previous_limit_ = limit;
        }
        return event.reasons != 0;
    }

    [[nodiscard]] bool has_capacity() const noexcept {
        return capture_->count < capture_->events.size();
    }

    void append(const AudioOutputDiagnosticEvent& event) noexcept {
        if (has_capacity()) {
            capture_->events[capture_->count++] = event;
        } else {
            ++capture_->dropped_events;
        }
    }

private:
    AudioOutputDiagnostics* capture_{nullptr};
    uint64_t previous_timestamp_ns_{0};
    uint64_t next_snapshot_ns_{0};
    int32_t previous_limit_{0};
};

} // namespace musicrat::backends::audio::detail
