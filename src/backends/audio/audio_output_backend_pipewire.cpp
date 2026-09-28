#include <musicrat/backends/audio/audio_output_backend.hpp>

#include <musicrat/config.hpp>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

namespace musicrat::backends::audio {
namespace {

void set_channel_positions(
    spa_audio_info_raw& format, const uint16_t channel_count) noexcept {
    format.position[0] = channel_count == 1
        ? SPA_AUDIO_CHANNEL_MONO
        : SPA_AUDIO_CHANNEL_FL;
    if (channel_count >= 2) {
        format.position[1] = SPA_AUDIO_CHANNEL_FR;
    }
    if (channel_count >= 3) {
        format.position[2] = SPA_AUDIO_CHANNEL_FC;
    }
    if (channel_count == 4) {
        format.position[2] = SPA_AUDIO_CHANNEL_RL;
        format.position[3] = SPA_AUDIO_CHANNEL_RR;
    } else if (channel_count == 5) {
        format.position[3] = SPA_AUDIO_CHANNEL_RL;
        format.position[4] = SPA_AUDIO_CHANNEL_RR;
    } else if (channel_count >= 6) {
        format.position[3] = SPA_AUDIO_CHANNEL_LFE;
        if (channel_count == 7) {
            format.position[4] = SPA_AUDIO_CHANNEL_RC;
            format.position[5] = SPA_AUDIO_CHANNEL_SL;
            format.position[6] = SPA_AUDIO_CHANNEL_SR;
        } else {
            format.position[4] = SPA_AUDIO_CHANNEL_RL;
            format.position[5] = SPA_AUDIO_CHANNEL_RR;
            if (channel_count >= 7) {
                format.position[6] = SPA_AUDIO_CHANNEL_SL;
            }
            if (channel_count >= 8) {
                format.position[7] = SPA_AUDIO_CHANNEL_SR;
            }
        }
    }
}

class PipeWireAudioOutputBackend final : public AudioOutputBackend {
public:
    ~PipeWireAudioOutputBackend() override {
        close();
    }

    [[nodiscard]] const char* name() const noexcept override {
        return "pipewire";
    }

    [[nodiscard]] bool open(const AudioOutputConfig& config) noexcept override {
        close();
        if (config.sample_rate_hz == 0 || config.channel_count == 0
            || config.channel_count > config::max_audio_channels
            || config.channel_count > 8
            || config.period_frames == 0) {
            return false;
        }

        static std::once_flag pipewire_init_flag;
        std::call_once(pipewire_init_flag, [] { pw_init(nullptr, nullptr); });

        channel_count_ = config.channel_count;
        read_position_.store(0, std::memory_order_relaxed);
        write_position_.store(0, std::memory_order_relaxed);
        started_.store(false, std::memory_order_relaxed);
        underruns_.store(0, std::memory_order_relaxed);
        underrun_frames_.store(0, std::memory_order_relaxed);
        stream_failures_.store(0, std::memory_order_relaxed);

        loop_ = pw_thread_loop_new("musicrat-audio-output", nullptr);
        if (loop_ == nullptr) {
            return false;
        }

        char latency[64]{};
        std::snprintf(
            latency, sizeof(latency), "%u/%u",
            config.period_frames, config.sample_rate_hz);
        auto* properties = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Music",
            PW_KEY_NODE_NAME, "musicrat.audio-device-sink",
            PW_KEY_NODE_DESCRIPTION, "MusicRaT Audio Device Sink",
            PW_KEY_NODE_LATENCY, latency,
            nullptr);
        if (properties == nullptr) {
            close();
            return false;
        }
        if (config.device_name != nullptr && config.device_name[0] != '\0') {
            pw_properties_set(properties, PW_KEY_TARGET_OBJECT, config.device_name);
        }

        stream_ = pw_stream_new_simple(
            pw_thread_loop_get_loop(loop_),
            "musicrat-audio-device-sink",
            properties,
            &stream_events_,
            this);
        if (stream_ == nullptr) {
            close();
            return false;
        }

        if (pw_thread_loop_start(loop_) < 0) {
            close();
            return false;
        }

        std::array<std::byte, 1024> format_buffer{};
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(
            format_buffer.data(), format_buffer.size());
        spa_audio_info_raw format{};
        format.format = SPA_AUDIO_FORMAT_F32;
        format.rate = config.sample_rate_hz;
        format.channels = config.channel_count;
        set_channel_positions(format, config.channel_count);
        const spa_pod* params[] = {
            spa_format_audio_raw_build(
                &builder, SPA_PARAM_EnumFormat, &format),
        };

        pw_thread_loop_lock(loop_);
        const int result = pw_stream_connect(
            stream_,
            PW_DIRECTION_OUTPUT,
            PW_ID_ANY,
            static_cast<pw_stream_flags>(
                PW_STREAM_FLAG_AUTOCONNECT
                | PW_STREAM_FLAG_MAP_BUFFERS
                | PW_STREAM_FLAG_RT_PROCESS),
            params,
            1);
        int wait_result = 0;
        auto state = state_.load(std::memory_order_acquire);
        while (result >= 0 && wait_result >= 0
               && (state == PW_STREAM_STATE_UNCONNECTED
                   || state == PW_STREAM_STATE_CONNECTING)) {
            wait_result = pw_thread_loop_timed_wait(loop_, 5);
            state = state_.load(std::memory_order_acquire);
        }
        pw_thread_loop_unlock(loop_);
        if (result < 0 || wait_result < 0
            || (state != PW_STREAM_STATE_PAUSED
                && state != PW_STREAM_STATE_STREAMING)) {
            close();
            return false;
        }

        open_.store(true, std::memory_order_release);
        return true;
    }

    void close() noexcept override {
        open_.store(false, std::memory_order_release);
        if (loop_ != nullptr) {
            pw_thread_loop_stop(loop_);
        }
        if (stream_ != nullptr) {
            pw_stream_destroy(stream_);
            stream_ = nullptr;
        }
        if (loop_ != nullptr) {
            pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
        }
        state_.store(PW_STREAM_STATE_UNCONNECTED, std::memory_order_relaxed);
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
                samples_[queue_frame * channel_count_ + channel] =
                    static_cast<float>(block.channels[channel][frame]);
            }
        }
        write_position_.store(
            write_position + block.frame_count, std::memory_order_release);
        started_.store(true, std::memory_order_release);
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

private:
    static constexpr std::size_t queue_capacity_frames =
        config::max_audio_frames * 4;

    static void process_callback(void* data) noexcept {
        static_cast<PipeWireAudioOutputBackend*>(data)->process();
    }

    static void state_changed_callback(
        void* data,
        pw_stream_state old_state,
        pw_stream_state state,
        const char*) noexcept {
        auto* backend = static_cast<PipeWireAudioOutputBackend*>(data);
        backend->state_.store(state, std::memory_order_release);
        if (state == PW_STREAM_STATE_PAUSED
            || state == PW_STREAM_STATE_STREAMING) {
            backend->open_.store(true, std::memory_order_release);
        } else if (state == PW_STREAM_STATE_ERROR
                   || state == PW_STREAM_STATE_UNCONNECTED) {
            backend->open_.store(false, std::memory_order_release);
            if (old_state == PW_STREAM_STATE_PAUSED
                || old_state == PW_STREAM_STATE_STREAMING) {
                backend->stream_failures_.fetch_add(
                    1, std::memory_order_relaxed);
            }
            const uint64_t write_position = backend->write_position_.load(
                std::memory_order_acquire);
            backend->read_position_.store(
                write_position, std::memory_order_release);
            backend->started_.store(false, std::memory_order_release);
        }
        if (backend->loop_ != nullptr) {
            pw_thread_loop_signal(backend->loop_, false);
        }
    }

    void process() noexcept {
        pw_buffer* pipewire_buffer = pw_stream_dequeue_buffer(stream_);
        if (pipewire_buffer == nullptr || pipewire_buffer->buffer == nullptr
            || pipewire_buffer->buffer->n_datas == 0) {
            return;
        }

        spa_data& data = pipewire_buffer->buffer->datas[0];
        if (data.data == nullptr || data.chunk == nullptr || channel_count_ == 0) {
            pw_stream_queue_buffer(stream_, pipewire_buffer);
            return;
        }

        const uint32_t stride = channel_count_ * sizeof(float);
        const uint32_t maximum_frames = data.maxsize / stride;
        const uint32_t requested_frames = pipewire_buffer->requested == 0
            ? maximum_frames
            : static_cast<uint32_t>(std::min<uint64_t>(
                pipewire_buffer->requested, maximum_frames));
        auto* output = static_cast<float*>(data.data);

        const uint64_t read_position =
            read_position_.load(std::memory_order_relaxed);
        const uint64_t write_position =
            write_position_.load(std::memory_order_acquire);
        const uint32_t available_frames = static_cast<uint32_t>(
            std::min<uint64_t>(write_position - read_position, requested_frames));

        for (uint32_t frame = 0; frame < available_frames; ++frame) {
            const std::size_t queue_frame = static_cast<std::size_t>(
                (read_position + frame) % queue_capacity_frames);
            std::memcpy(
                output + static_cast<std::size_t>(frame) * channel_count_,
                samples_.data() + queue_frame * channel_count_,
                stride);
        }
        if (available_frames < requested_frames) {
            std::memset(
                output + static_cast<std::size_t>(available_frames) * channel_count_,
                0,
                static_cast<std::size_t>(requested_frames - available_frames)
                    * stride);
            if (started_.load(std::memory_order_acquire)) {
                underruns_.fetch_add(1, std::memory_order_relaxed);
                underrun_frames_.fetch_add(
                    requested_frames - available_frames,
                    std::memory_order_relaxed);
            }
        }

        read_position_.store(
            read_position + available_frames, std::memory_order_release);
        data.chunk->offset = 0;
        data.chunk->stride = static_cast<int32_t>(stride);
        data.chunk->size = requested_frames * stride;
        pipewire_buffer->size = requested_frames;
        pw_stream_queue_buffer(stream_, pipewire_buffer);
    }

    inline static const pw_stream_events stream_events_ = [] {
        pw_stream_events events{};
        events.version = PW_VERSION_STREAM_EVENTS;
        events.state_changed = state_changed_callback;
        events.process = process_callback;
        return events;
    }();

    std::array<float, queue_capacity_frames * config::max_audio_channels>
        samples_{};
    std::atomic<uint64_t> read_position_{0};
    std::atomic<uint64_t> write_position_{0};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> underrun_frames_{0};
    std::atomic<uint64_t> stream_failures_{0};
    std::atomic<bool> started_{false};
    std::atomic<bool> open_{false};
    std::atomic<pw_stream_state> state_{PW_STREAM_STATE_UNCONNECTED};
    pw_thread_loop* loop_{nullptr};
    pw_stream* stream_{nullptr};
    uint16_t channel_count_{0};
};

} // namespace

std::unique_ptr<AudioOutputBackend> make_audio_output_backend() {
    return std::make_unique<PipeWireAudioOutputBackend>();
}

} // namespace musicrat::backends::audio