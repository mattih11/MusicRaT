#include <musicrat/backends/audio/audio_output_backend.hpp>

#include <musicrat/config.hpp>
#include <corerat/platform/timestamp.hpp>

#include "playback_diagnostics.hpp"
#include "playback_preroll.hpp"
#include "queue_drift_controller.hpp"
#include "wait_scheduling.hpp"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

namespace musicrat::backends::audio {
namespace {

class AlsaAudioOutputBackend final : public AudioOutputBackend {
public:
    ~AlsaAudioOutputBackend() override {
        close();
    }

    [[nodiscard]] const char* name() const noexcept override {
        return "alsa";
    }

    [[nodiscard]] bool open(const AudioOutputConfig& config) noexcept override {
        close();
        if (config.sample_rate_hz == 0 || config.channel_count == 0
            || config.channel_count > config::max_audio_channels
            || config.period_frames == 0
            || config.period_frames > config::max_audio_frames) {
            return false;
        }

        const char* device_name = config.device_name;
        if (device_name == nullptr || device_name[0] == '\0') {
            device_name = "default";
        }
        if (snd_pcm_open(
                &pcm_, device_name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)
            < 0) {
            pcm_ = nullptr;
            return false;
        }

        const unsigned int latency_us = static_cast<unsigned int>(
            std::max<uint64_t>(
                1000,
                static_cast<uint64_t>(config.period_frames) * 2000000ULL
                    / config.sample_rate_hz));
        if (snd_pcm_set_params(
                pcm_,
                SND_PCM_FORMAT_S16_LE,
                SND_PCM_ACCESS_RW_INTERLEAVED,
                config.channel_count,
                config.sample_rate_hz,
                0,
                latency_us)
            < 0) {
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
            return false;
        }

        snd_pcm_uframes_t buffer_frames = 0;
        snd_pcm_uframes_t device_period_frames = 0;
        if (snd_pcm_get_params(pcm_, &buffer_frames, &device_period_frames) < 0
            || device_period_frames == 0
            || device_period_frames > queue_capacity_frames / 2) {
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
            return false;
        }

        channel_count_ = config.channel_count;
        period_frames_ = static_cast<uint32_t>(device_period_frames);
        diagnostics_ = config.diagnostics;
        if (diagnostics_ != nullptr) {
            snd_pcm_hw_params_t* hardware = nullptr;
            snd_pcm_hw_params_alloca(&hardware);
            unsigned int actual_rate = 0;
            if (snd_pcm_hw_params_current(pcm_, hardware) < 0
                || snd_pcm_hw_params_get_rate(hardware, &actual_rate, nullptr) < 0) {
                snd_pcm_close(pcm_);
                pcm_ = nullptr;
                diagnostics_ = nullptr;
                return false;
            }
            const auto storage = diagnostics_->events;
            const bool wait_scheduling = diagnostics_->wait_scheduling;
            *diagnostics_ = AudioOutputDiagnostics{
                .events = storage, .wait_scheduling = wait_scheduling};
            diagnostics_->supported = true;
            diagnostics_->start_timestamp_ns = corerat::Time::now();
            diagnostics_->device_buffer_frames = buffer_frames;
            diagnostics_->device_period_frames = device_period_frames;
            diagnostics_->device_sample_rate_hz = actual_rate;
            diagnostics_->slow_operation_threshold_ns = std::max<uint64_t>(
                4 * device_period_frames, 2ULL * config.period_frames)
                * 1'000'000'000ULL / config.sample_rate_hz;
        }
        diagnostic_recorder_.reset(diagnostics_);
        diagnostic_source_frames_ = 0;
        diagnostic_device_frames_ = 0;
        preroll_.reset(
            config.period_frames, queue_capacity_frames - config::max_audio_frames,
            buffer_frames);
        drift_controller_.reset(preroll_.target_frames(period_frames_));
        read_position_.store(0, std::memory_order_relaxed);
        write_position_.store(0, std::memory_order_relaxed);
        read_fraction_ = 0.0;
        queue_high_water_frames_.store(0, std::memory_order_relaxed);
        rate_correction_ppm_.store(0, std::memory_order_relaxed);
        maximum_rate_correction_ppm_.store(0, std::memory_order_relaxed);
        underruns_.store(0, std::memory_order_relaxed);
        underrun_frames_.store(0, std::memory_order_relaxed);
        stream_failures_.store(0, std::memory_order_relaxed);
        stopping_.store(false, std::memory_order_relaxed);
        open_.store(true, std::memory_order_release);

        try {
            worker_ = std::thread{&AlsaAudioOutputBackend::run, this};
        } catch (...) {
            open_.store(false, std::memory_order_release);
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
            return false;
        }
        return true;
    }

    void close() noexcept override {
        open_.store(false, std::memory_order_release);
        stopping_.store(true, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
        if (pcm_ != nullptr) {
            snd_pcm_drop(pcm_);
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
        }
        diagnostic_recorder_.reset(nullptr);
        diagnostics_ = nullptr;
    }

    [[nodiscard]] bool write(
        const CommRaT::Messages::AudioBlock& block) noexcept override {
        if (!open_.load(std::memory_order_acquire)
            || block.channel_count != channel_count_) {
            return false;
        }

        const uint64_t write_position =
            write_position_.load(std::memory_order_relaxed);
        const uint64_t read_position =
            read_position_.load(std::memory_order_acquire);
        const uint64_t used_frames = write_position - read_position;
        if (block.frame_count > queue_capacity_frames - used_frames) {
            return false;
        }

        for (uint32_t frame = 0; frame < block.frame_count; ++frame) {
            const std::size_t queue_frame = static_cast<std::size_t>(
                (write_position + frame) % queue_capacity_frames);
            for (uint16_t channel = 0; channel < channel_count_; ++channel) {
                const auto sample = static_cast<float>(
                    block.channels[channel][frame]);
                samples_[queue_frame * config::max_audio_channels + channel] =
                    static_cast<int16_t>(std::lrint(
                        std::clamp(sample, -1.0F, 1.0F) * 32767.0F));
            }
        }
        write_position_.store(
            write_position + block.frame_count, std::memory_order_release);
        queue_high_water_frames_.store(
            std::max<uint64_t>(
                queue_high_water_frames_.load(std::memory_order_relaxed),
                used_frames + block.frame_count),
            std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] uint64_t underrun_count() const noexcept override {
        return underruns_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t underrun_frame_count() const noexcept override {
        return underrun_frames_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t stream_failure_count() const noexcept override {
        return stream_failures_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t queued_frames() const noexcept override {
        const uint64_t read_position =
            read_position_.load(std::memory_order_acquire);
        const uint64_t write_position =
            write_position_.load(std::memory_order_acquire);
        return write_position >= read_position
            ? std::min<uint64_t>(
                write_position - read_position, queue_capacity_frames)
            : 0;
    }

    [[nodiscard]] uint64_t queue_high_water_frames() const noexcept override {
        return queue_high_water_frames_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t queue_capacity() const noexcept override {
        return queue_capacity_frames;
    }

    [[nodiscard]] int32_t rate_correction_ppm() const noexcept override {
        return rate_correction_ppm_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint32_t maximum_rate_correction_ppm() const noexcept override {
        return maximum_rate_correction_ppm_.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t queue_capacity_frames =
        config::max_audio_frames * 4;

    void run() noexcept {
        std::array<int16_t,
            config::max_audio_frames * config::max_audio_channels> output{};
        detail::WaitSchedulingObserver scheduling;
        int scheduling_open_result = 0;
        const bool observe_waits =
            diagnostics_ != nullptr && diagnostics_->wait_scheduling;
        if (observe_waits) {
            scheduling_open_result = scheduling.open();
            if (scheduling_open_result < 0) {
                AudioOutputDiagnosticEvent event{
                    .timestamp_ns = corerat::Time::now(),
                    .scheduling_result = scheduling_open_result,
                    .reasons = diagnostic_scheduling_error};
                ++diagnostics_->scheduling_failures;
                record_diagnostic(event);
                open_.store(false, std::memory_order_release);
                return;
            }
        }

        while (!stopping_.load(std::memory_order_acquire)) {
            AudioOutputDiagnosticEvent event{};
            if (diagnostic_recorder_.enabled()) {
                event.timestamp_ns = corerat::Time::now();
                event.correction_ppm = rate_correction_ppm();
            }
            const uint64_t read_position =
                read_position_.load(std::memory_order_relaxed);
            const uint64_t write_position =
                write_position_.load(std::memory_order_acquire);
            const uint32_t available_frames = static_cast<uint32_t>(
                std::min<uint64_t>(
                    write_position - read_position, config::max_audio_frames));
            event.queued_before = write_position - read_position;
            if (available_frames == 0
                || !preroll_.ready(write_position - read_position, period_frames_)) {
                record_diagnostic(event);
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                continue;
            }

            const int32_t correction = drift_controller_.update(
                write_position - read_position);
            event.correction_ppm = correction;
            record_rate_correction(correction);
            const double source_step = drift_controller_.source_step();
            double source_phase = read_fraction_;
            uint32_t output_frames = 0;
            for (; output_frames < available_frames; ++output_frames) {
                const uint64_t source_offset =
                    static_cast<uint64_t>(source_phase);
                if (source_offset + 1 >= write_position - read_position) {
                    break;
                }
                const std::size_t queue_frame = static_cast<std::size_t>(
                    (read_position + source_offset) % queue_capacity_frames);
                const std::size_t next_queue_frame =
                    (queue_frame + 1) % queue_capacity_frames;
                const double fraction = source_phase - source_offset;
                for (uint16_t channel = 0; channel < channel_count_; ++channel) {
                    const double first =
                        samples_[queue_frame * config::max_audio_channels + channel];
                    const double second =
                        samples_[next_queue_frame * config::max_audio_channels + channel];
                    output[static_cast<std::size_t>(output_frames) * channel_count_
                           + channel] = static_cast<int16_t>(std::lrint(
                        std::clamp(
                            detail::linear_interpolate(
                                first, second, fraction),
                            -32768.0, 32767.0)));
                }
                source_phase += source_step;
            }
            if (output_frames == 0) {
                record_diagnostic(event);
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                continue;
            }

            const uint64_t write_start = diagnostic_recorder_.enabled()
                ? corerat::Time::now() : 0;
            const snd_pcm_sframes_t written =
                snd_pcm_writei(pcm_, output.data(), output_frames);
            if (diagnostic_recorder_.enabled()) {
                event.write_duration_ns = corerat::Time::now() - write_start;
                event.requested_frames = output_frames;
                event.write_result = written;
            }
            if (written > 0) {
                const uint64_t consumed_frames = detail::advance_source_cursor(
                    read_fraction_, static_cast<uint32_t>(written),
                    source_step, read_fraction_);
                read_position_.store(
                    read_position + consumed_frames,
                    std::memory_order_release);
                if (diagnostic_recorder_.enabled()) {
                    diagnostic_source_frames_ += consumed_frames;
                    diagnostic_device_frames_ += static_cast<uint64_t>(written);
                }
                record_diagnostic(event);
                continue;
            }
            if (written == -EAGAIN) {
                detail::ThreadSchedulingCounters before{};
                if (observe_waits) {
                    event.scheduling_before_timestamp_ns = corerat::Time::now();
                    event.scheduling_result = scheduling.read(before);
                }
                const uint64_t wait_start = diagnostic_recorder_.enabled()
                    ? corerat::Time::now() : 0;
                const int wait_result = snd_pcm_wait(pcm_, 100);
                if (diagnostic_recorder_.enabled()) {
                    event.wait_duration_ns = corerat::Time::now() - wait_start;
                    event.wait_start_timestamp_ns = wait_start;
                    event.wait_result = wait_result;
                    if (wait_result <= 0) {
                        event.reasons |= diagnostic_wait_error;
                    }
                }
                if (observe_waits) {
                    detail::ThreadSchedulingCounters after{};
                    const int result = scheduling.read(after);
                    event.scheduling_after_timestamp_ns = corerat::Time::now();
                    if (event.scheduling_result == 0) {
                        event.scheduling_result = result;
                    }
                    if (event.scheduling_result == 0) {
                        if (after.runtime_ns < before.runtime_ns
                            || after.runnable_delay_ns < before.runnable_delay_ns
                            || after.timeslices < before.timeslices) {
                            event.scheduling_result = -ERANGE;
                        } else {
                            event.scheduling_runtime_ns =
                                after.runtime_ns - before.runtime_ns;
                            event.scheduling_runnable_delay_ns =
                                after.runnable_delay_ns - before.runnable_delay_ns;
                            event.scheduling_timeslices =
                                after.timeslices - before.timeslices;
                        }
                    }
                    if (event.scheduling_result < 0) {
                        ++diagnostics_->scheduling_failures;
                        event.reasons |= diagnostic_scheduling_error;
                    }
                }
                record_diagnostic(event);
                continue;
            }
            event.reasons |= diagnostic_write_error;
            if (written == -EPIPE) {
                underruns_.fetch_add(1, std::memory_order_relaxed);
                underrun_frames_.fetch_add(
                    period_frames_, std::memory_order_relaxed);
                preroll_.rebuffer();
            }
            const int recovery_result =
                snd_pcm_recover(pcm_, static_cast<int>(written), 1);
            event.recovery_result = recovery_result;
            record_diagnostic(event);
            if (recovery_result < 0) {
                stream_failures_.fetch_add(1, std::memory_order_relaxed);
                open_.store(false, std::memory_order_release);
                return;
            }
        }
    }

    void record_diagnostic(AudioOutputDiagnosticEvent& event) noexcept {
        if (!diagnostic_recorder_.select(event)) {
            return;
        }
        event.queued_after = queued_frames();
        event.source_frames_consumed = diagnostic_source_frames_;
        event.device_frames_written = diagnostic_device_frames_;
        if (diagnostic_recorder_.has_capacity()) {
            event.observation_timestamp_ns = corerat::Time::now();
            snd_pcm_sframes_t available = 0;
            snd_pcm_sframes_t delay = 0;
            event.device_status_result = snd_pcm_avail_delay(
                pcm_, &available, &delay);
            event.device_available_frames = available;
            event.device_delay_frames = delay;
            event.device_state = static_cast<int32_t>(snd_pcm_state(pcm_));
            diagnostics_->status_failures += event.device_status_result < 0;
        }
        diagnostic_recorder_.append(event);
    }

    void record_rate_correction(int32_t correction) noexcept {
        rate_correction_ppm_.store(correction, std::memory_order_relaxed);
        const uint32_t magnitude = static_cast<uint32_t>(
            correction < 0 ? -correction : correction);
        if (magnitude > maximum_rate_correction_ppm_.load(
                            std::memory_order_relaxed)) {
            maximum_rate_correction_ppm_.store(
                magnitude, std::memory_order_relaxed);
        }
    }

    std::array<int16_t,
        queue_capacity_frames * config::max_audio_channels> samples_{};
    std::atomic<uint64_t> read_position_{0};
    std::atomic<uint64_t> write_position_{0};
    std::atomic<uint64_t> queue_high_water_frames_{0};
    std::atomic<int32_t> rate_correction_ppm_{0};
    std::atomic<uint32_t> maximum_rate_correction_ppm_{0};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> underrun_frames_{0};
    std::atomic<uint64_t> stream_failures_{0};
    std::atomic<bool> stopping_{true};
    std::atomic<bool> open_{false};
    std::thread worker_;
    detail::PlaybackPreroll preroll_;
    detail::QueueDriftController drift_controller_;
    double read_fraction_{0.0};
    snd_pcm_t* pcm_{nullptr};
    uint32_t period_frames_{0};
    uint16_t channel_count_{0};
    AudioOutputDiagnostics* diagnostics_{nullptr};
    detail::PlaybackDiagnosticRecorder diagnostic_recorder_;
    uint64_t diagnostic_source_frames_{0};
    uint64_t diagnostic_device_frames_{0};
};

} // namespace

std::unique_ptr<AudioOutputBackend> make_audio_output_backend() {
    return std::make_unique<AlsaAudioOutputBackend>();
}

} // namespace musicrat::backends::audio