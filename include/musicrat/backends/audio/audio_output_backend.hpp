#pragma once

#include <musicrat/protocol/audio_block.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace musicrat::backends::audio {

enum AudioOutputDiagnosticReason : uint32_t {
    diagnostic_snapshot = 1U << 0,
    diagnostic_worker_gap = 1U << 1,
    diagnostic_slow_write = 1U << 2,
    diagnostic_slow_wait = 1U << 3,
    diagnostic_write_error = 1U << 4,
    diagnostic_wait_error = 1U << 5,
    diagnostic_correction_limit = 1U << 6,
    diagnostic_scheduling_error = 1U << 7,
};

struct AudioOutputDiagnosticEvent {
    uint64_t timestamp_ns{0};
    uint64_t observation_timestamp_ns{0};
    uint64_t worker_gap_ns{0};
    uint64_t write_duration_ns{0};
    uint64_t wait_duration_ns{0};
    uint64_t wait_start_timestamp_ns{0};
    uint64_t scheduling_before_timestamp_ns{0};
    uint64_t scheduling_after_timestamp_ns{0};
    uint64_t scheduling_runtime_ns{0};
    uint64_t scheduling_runnable_delay_ns{0};
    uint64_t scheduling_timeslices{0};
    int32_t scheduling_result{0};
    uint64_t queued_before{0};
    uint64_t queued_after{0};
    uint64_t source_frames_consumed{0};
    uint64_t device_frames_written{0};
    int64_t write_result{0};
    int64_t device_available_frames{0};
    int64_t device_delay_frames{0};
    uint32_t requested_frames{0};
    uint32_t reasons{0};
    int32_t correction_ppm{0};
    int32_t wait_result{0};
    int32_t recovery_result{0};
    int32_t device_state{0};
    int32_t device_status_result{0};
};

// Caller-owned storage must outlive close(). Read it only after close() joins
// the worker; no live access or reuse by another backend is permitted.
struct AudioOutputDiagnostics {
    std::span<AudioOutputDiagnosticEvent> events{};
    std::size_t count{0};
    uint64_t dropped_events{0};
    uint64_t status_failures{0};
    uint64_t scheduling_failures{0};
    uint64_t start_timestamp_ns{0};
    uint64_t slow_operation_threshold_ns{0};
    uint64_t device_buffer_frames{0};
    uint64_t device_period_frames{0};
    uint32_t device_sample_rate_hz{0};
    bool supported{false};
    bool wait_scheduling{false};
};

struct AudioOutputConfig {
    const char* device_name{nullptr};
    uint32_t sample_rate_hz{0};
    uint16_t channel_count{0};
    uint32_t period_frames{0};
    AudioOutputDiagnostics* diagnostics{nullptr};
};

class AudioOutputBackend {
public:
    virtual ~AudioOutputBackend() = default;

    [[nodiscard]] virtual const char* name() const noexcept = 0;
    [[nodiscard]] virtual bool open(const AudioOutputConfig& config) noexcept = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual bool write(
        const CommRaT::Messages::AudioBlock& block) noexcept = 0;
    [[nodiscard]] virtual uint64_t underrun_count() const noexcept = 0;
    [[nodiscard]] virtual uint64_t underrun_frame_count() const noexcept = 0;
    [[nodiscard]] virtual uint64_t stream_failure_count() const noexcept = 0;
    [[nodiscard]] virtual uint64_t queued_frames() const noexcept = 0;
    [[nodiscard]] virtual uint64_t queue_high_water_frames() const noexcept = 0;
    [[nodiscard]] virtual uint64_t queue_capacity() const noexcept = 0;
    [[nodiscard]] virtual int32_t rate_correction_ppm() const noexcept = 0;
    [[nodiscard]] virtual uint32_t maximum_rate_correction_ppm() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<AudioOutputBackend>
make_audio_output_backend();

} // namespace musicrat::backends::audio