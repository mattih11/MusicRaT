#pragma once

#include <musicrat/dsp/waveforms.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CommRaT {

template <typename WaveformType>
class Oscillator : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::Oscillator>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::Oscillator>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        return musicrat::launcher::audio_source_metadata("sample_rate_hz", 1);
    }

    explicit Oscillator(const commrat::ModuleConfig& config)
        : Base(config)
        , samples_per_period_(calculate_frame_count(config, this->params_.sample_rate_hz)) {
        this->template register_command_handler<
            0, Messages::ResetPhase, &Oscillator::handle_reset_phase>(*this);
        normalize_params();
    }

protected:
    void process(Messages::AudioBlock& output) override {
        for (auto& channel : output.channels) {
            channel.clear();
        }

        output.sample_rate_hz = this->params_.sample_rate_hz;
        output.timestamp_ns = commrat::Time::now();
        output.sequence_number = sequence_number_++;
        output.frame_count = static_cast<uint32_t>(samples_per_period_);
        output.channel_count = 1;
        output.flags = 0;

        for (size_t frame = 0; frame < samples_per_period_; ++frame) {
            output.channels[0].push_back(this->params_.enabled ? tick() : 0.0F);
        }
    }

    void on_params_changed() override {
        normalize_params();
        samples_per_period_ = calculate_frame_count(this->config_, this->params_.sample_rate_hz);
    }

private:
    void handle_reset_phase(const Messages::ResetPhase&, Messages::ResetPhase::Reply& reply) {
        phase_ = 0.0;
        reply.success = true;
    }

    Messages::AudioBlock::Sample tick() {
        double current_phase = phase_ + this->params_.phase_offset;
        current_phase -= std::floor(current_phase);

        const double sample = waveform_(current_phase) * this->params_.amplitude;
        phase_ += this->params_.frequency_hz / this->params_.sample_rate_hz;
        phase_ -= std::floor(phase_);
        return static_cast<Messages::AudioBlock::Sample>(sample);
    }

    static size_t calculate_frame_count(
        const commrat::ModuleConfig& config,
        double sample_rate_hz) {
        if (!config.period.has_value() || config.period->count() <= 0) {
            throw std::invalid_argument("Oscillator requires a positive period");
        }
        if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
            throw std::invalid_argument("Oscillator requires a positive sample rate");
        }

        const double frames = sample_rate_hz
            * static_cast<double>(config.period->count()) / 1000.0;
        const auto rounded_frames = static_cast<size_t>(std::llround(frames));
        if (rounded_frames == 0 || rounded_frames > Messages::AudioBlock::MAX_FRAMES) {
            throw std::invalid_argument("Oscillator period exceeds audio block capacity");
        }
        return rounded_frames;
    }

    void normalize_params() {
        this->params_.frequency_hz = std::max(0.0, this->params_.frequency_hz);
        this->params_.amplitude = std::clamp(
            this->params_.amplitude,
            Messages::AudioBlock::Sample{0},
            Messages::AudioBlock::Sample{1});
        this->params_.phase_offset -= std::floor(this->params_.phase_offset);
    }

    WaveformType waveform_{};
    double phase_{0.0};
    size_t samples_per_period_{0};
    uint64_t sequence_number_{0};
};

} // namespace CommRaT