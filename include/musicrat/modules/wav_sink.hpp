#pragma once

#include <musicrat/backends/audio/wav_writer.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/wav_sink.hpp>

#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace CommRaT {

class WavSink : public MusicRaT::Module2<
    commrat::Input<Messages::AudioBlock>,
    commrat::Params<Parameters::WavSink>
> {
    using Base = MusicRaT::Module2<
        commrat::Input<Messages::AudioBlock>,
        commrat::Params<Parameters::WavSink>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        return musicrat::launcher::audio_sink_metadata(
            "sample_rate_hz", "channel_count");
    }

    explicit WavSink(const commrat::ModuleConfig& config)
        : Base(config) {
        validate_params();
    }

    [[nodiscard]] uint64_t blocks_written() const noexcept {
        return blocks_written_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t blocks_rejected() const noexcept {
        return blocks_rejected_.load(std::memory_order_relaxed);
    }

protected:
    commrat::LifecycleResult on_enable() override {
        const corerat::FileConfig file_config{
            .proxy_buf_size = musicrat::config::file_proxy_bytes,
        };
        if (!writer_.open(
                this->params_.path.c_str(),
                this->params_.sample_rate_hz,
                this->params_.channel_count,
                file_config)) {
            return commrat::LifecycleResult::Failed;
        }
        return commrat::LifecycleResult::Success;
    }

    void on_disable() override {
        writer_.close();
    }

    void process(const Messages::AudioBlock& input) override {
        if (writer_.write(input)) {
            blocks_written_.fetch_add(1, std::memory_order_relaxed);
        } else {
            blocks_rejected_.fetch_add(1, std::memory_order_relaxed);
        }
    }

private:
    void validate_params() const {
        if (this->params_.path.empty()) {
            throw std::invalid_argument("WavSink requires a non-empty path");
        }
        if (this->params_.sample_rate_hz == 0) {
            throw std::invalid_argument("WavSink requires a positive sample rate");
        }
        if (this->params_.channel_count == 0
            || this->params_.channel_count > Messages::AudioBlock::MAX_CHANNELS) {
            throw std::invalid_argument("WavSink channel count exceeds audio block capacity");
        }
    }

    musicrat::backends::audio::WavWriter writer_{};
    std::atomic<uint64_t> blocks_written_{0};
    std::atomic<uint64_t> blocks_rejected_{0};
};

} // namespace CommRaT