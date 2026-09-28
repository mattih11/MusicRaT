#pragma once

#include <musicrat/backends/audio/audio_output_backend.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/audio_device_sink.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace CommRaT {

class AudioDeviceSink : public MusicRaT::Module2<
    commrat::Input<Messages::AudioBlock>,
    commrat::Params<Parameters::AudioDeviceSink>
> {
    using Base = MusicRaT::Module2<
        commrat::Input<Messages::AudioBlock>,
        commrat::Params<Parameters::AudioDeviceSink>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        auto metadata = audio_sink_metadata(
            "sample_rate_hz", "channel_count");
        metadata.musicrat_ports.ports = {{
            .id = "audio_in",
            .display_name = "Audio In",
            .direction = PORT_DIRECTION_INPUT,
            .port_index = 0,
            .domain = PORT_DOMAIN_AUDIO,
        }};
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::AUDIO_DEVICE_SINK_DEVICE_PARAMETER_ID,
                .name = "device_name",
                .display_name = "Device",
                .group = "Audio Device",
                .kind = PARAMETER_KIND_TEXT,
                .unit = "",
                .choices = {},
            },
            {
                .id = Parameters::AUDIO_DEVICE_SINK_SAMPLE_RATE_PARAMETER_ID,
                .name = "sample_rate_hz",
                .display_name = "Sample Rate",
                .group = "Audio",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "Hz",
                .minimum = 8000.0,
                .maximum = 192000.0,
                .step = 1.0,
                .choices = {},
            },
            {
                .id = Parameters::AUDIO_DEVICE_SINK_CHANNEL_COUNT_PARAMETER_ID,
                .name = "channel_count",
                .display_name = "Channels",
                .group = "Audio",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "",
                .minimum = 1.0,
                .maximum = static_cast<double>(Messages::AudioBlock::MAX_CHANNELS),
                .step = 1.0,
                .choices = {},
            },
            {
                .id = Parameters::AUDIO_DEVICE_SINK_PERIOD_FRAMES_PARAMETER_ID,
                .name = "period_frames",
                .display_name = "Period",
                .group = "Audio Device",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "frames",
                .minimum = 1.0,
                .maximum = static_cast<double>(Messages::AudioBlock::MAX_FRAMES),
                .step = 1.0,
                .choices = {},
            },
        };
        return metadata;
    }

    explicit AudioDeviceSink(const commrat::ModuleConfig& config)
        : AudioDeviceSink(
            config, musicrat::backends::audio::make_audio_output_backend()) {}

    AudioDeviceSink(
        const commrat::ModuleConfig& config,
        std::unique_ptr<musicrat::backends::audio::AudioOutputBackend> backend)
        : Base(config), backend_(std::move(backend)) {
        validate_params();
        if (!backend_) {
            throw std::invalid_argument("AudioDeviceSink requires a backend");
        }
    }

    [[nodiscard]] const char* backend_name() const noexcept {
        return backend_->name();
    }

    [[nodiscard]] uint64_t blocks_queued() const noexcept {
        return blocks_queued_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t blocks_rejected() const noexcept {
        return blocks_rejected_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t underruns() const noexcept {
        return backend_->underrun_count();
    }

    [[nodiscard]] uint64_t underrun_frames() const noexcept {
        return backend_->underrun_frame_count();
    }

    [[nodiscard]] uint64_t stream_failures() const noexcept {
        return backend_->stream_failure_count();
    }

protected:
    commrat::LifecycleResult on_enable() override {
        const musicrat::backends::audio::AudioOutputConfig output_config{
            .device_name = this->params_.device_name.c_str(),
            .sample_rate_hz = this->params_.sample_rate_hz,
            .channel_count = this->params_.channel_count,
            .period_frames = this->params_.period_frames,
        };
        return backend_->open(output_config)
            ? commrat::LifecycleResult::Success
            : commrat::LifecycleResult::Failed;
    }

    void on_disable() override {
        backend_->close();
    }

    void process(const Messages::AudioBlock& input) override {
        if (musicrat::validate_audio_block(input)
                != musicrat::AudioBlockValidationError::None
            || input.sample_rate_hz
                != static_cast<double>(this->params_.sample_rate_hz)
            || input.channel_count != this->params_.channel_count
            || !backend_->write(input)) {
            blocks_rejected_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        blocks_queued_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    void validate_params() const {
        if (this->params_.sample_rate_hz == 0) {
            throw std::invalid_argument(
                "AudioDeviceSink requires a positive sample rate");
        }
        if (this->params_.channel_count == 0
            || this->params_.channel_count > Messages::AudioBlock::MAX_CHANNELS) {
            throw std::invalid_argument(
                "AudioDeviceSink channel count exceeds audio block capacity");
        }
        if (this->params_.period_frames == 0
            || this->params_.period_frames > Messages::AudioBlock::MAX_FRAMES) {
            throw std::invalid_argument(
                "AudioDeviceSink period exceeds audio block capacity");
        }
    }

    std::unique_ptr<musicrat::backends::audio::AudioOutputBackend> backend_;
    std::atomic<uint64_t> blocks_queued_{0};
    std::atomic<uint64_t> blocks_rejected_{0};
};

} // namespace CommRaT