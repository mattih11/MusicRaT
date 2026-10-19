#include <musicrat/backends/audio/audio_output_backend.hpp>

#include <musicrat/config.hpp>

#include "playback_preroll.hpp"
#include "queue_drift_controller.hpp"

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

        while (!stopping_.load(std::memory_order_acquire)) {
            const uint64_t read_position =
                read_position_.load(std::memory_order_relaxed);
            const uint64_t write_position =
                write_position_.load(std::memory_order_acquire);
            const uint32_t available_frames = static_cast<uint32_t>(
                std::min<uint64_t>(
                    write_position - read_position, config::max_audio_frames));
            if (available_frames == 0
                || !preroll_.ready(write_position - read_position, period_frames_)) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                continue;
            }

            const int32_t correction = drift_controller_.update(
                write_position - read_position);
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
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                continue;
            }

            const snd_pcm_sframes_t written =
                snd_pcm_writei(pcm_, output.data(), output_frames);
            if (written > 0) {
                const uint64_t consumed_frames = detail::advance_source_cursor(
                    read_fraction_, static_cast<uint32_t>(written),
                    source_step, read_fraction_);
                read_position_.store(
                    read_position + consumed_frames,
                    std::memory_order_release);
                continue;
            }
            if (written == -EAGAIN) {
                snd_pcm_wait(pcm_, 100);
                continue;
            }
            if (written == -EPIPE) {
                underruns_.fetch_add(1, std::memory_order_relaxed);
                underrun_frames_.fetch_add(
                    period_frames_, std::memory_order_relaxed);
                preroll_.rebuffer();
            }
            if (snd_pcm_recover(pcm_, static_cast<int>(written), 1) < 0) {
                stream_failures_.fetch_add(1, std::memory_order_relaxed);
                open_.store(false, std::memory_order_release);
                return;
            }
        }
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
};

} // namespace

std::unique_ptr<AudioOutputBackend> make_audio_output_backend() {
    return std::make_unique<AlsaAudioOutputBackend>();
}

} // namespace musicrat::backends::audio