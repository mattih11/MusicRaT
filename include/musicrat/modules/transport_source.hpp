#pragma once

#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace CommRaT {

class TransportSource : public MusicRaT::Module2<
    commrat::Output<Messages::TransportBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::TransportSource>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::TransportBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::TransportSource>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {{
            .id = "transport",
            .display_name = "Transport",
            .direction = PORT_DIRECTION_OUTPUT,
            .port_index = 0,
            .domain = PORT_DOMAIN_TRANSPORT,
        }};
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::TRANSPORT_TEMPO_PARAMETER_ID,
                .name = "tempo_bpm",
                .display_name = "Tempo",
                .group = "Transport",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "BPM",
                .minimum = 20.0,
                .maximum = 400.0,
                .step = 0.1,
            },
            {
                .id = Parameters::TRANSPORT_INITIAL_BEAT_PARAMETER_ID,
                .name = "initial_beat_position",
                .display_name = "Initial Beat",
                .group = "Transport",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "beats",
                .minimum = 0.0,
                .maximum = 1000000000.0,
                .step = 0.01,
            },
            {
                .id = Parameters::TRANSPORT_SAMPLE_RATE_PARAMETER_ID,
                .name = "sample_rate_hz",
                .display_name = "Sample Rate",
                .group = "Audio",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "Hz",
                .minimum = 8000.0,
                .maximum = 192000.0,
                .step = 1.0,
            },
            {
                .id = Parameters::TRANSPORT_BEATS_PER_BAR_PARAMETER_ID,
                .name = "beats_per_bar",
                .display_name = "Beats per Bar",
                .group = "Meter",
                .kind = PARAMETER_KIND_INTEGER,
                .minimum = 1.0,
                .maximum = 32.0,
                .step = 1.0,
            },
            {
                .id = Parameters::TRANSPORT_BEAT_UNIT_PARAMETER_ID,
                .name = "beat_unit",
                .display_name = "Beat Unit",
                .group = "Meter",
                .kind = PARAMETER_KIND_CHOICE,
                .choices = {
                    {.value = 1.0, .label = "Whole"},
                    {.value = 2.0, .label = "Half"},
                    {.value = 4.0, .label = "Quarter"},
                    {.value = 8.0, .label = "Eighth"},
                    {.value = 16.0, .label = "Sixteenth"},
                },
            },
            {
                .id = Parameters::TRANSPORT_STATE_PARAMETER_ID,
                .name = "state",
                .display_name = "Initial State",
                .group = "Transport",
                .kind = PARAMETER_KIND_CHOICE,
                .choices = {
                    {.value = Messages::TRANSPORT_STOPPED, .label = "Stopped"},
                    {.value = Messages::TRANSPORT_PLAYING, .label = "Playing"},
                    {.value = Messages::TRANSPORT_PAUSED, .label = "Paused"},
                    {.value = Messages::TRANSPORT_RECORDING, .label = "Recording"},
                },
            },
        };
        return metadata;
    }

    explicit TransportSource(const commrat::ModuleConfig& config)
        : Base(config)
        , period_ms_(static_cast<uint64_t>(config.period->count()))
        , frames_per_period_(calculate_frame_count(
            config, this->params_.sample_rate_hz))
        , beat_position_(this->params_.initial_beat_position) {
        validate_params();
    }

protected:
    void process(Messages::TransportBlock& output) override {
        output = {
            .beat_position = beat_position_,
            .tempo_bpm = this->params_.tempo_bpm,
            .transport_frame = transport_frame_,
            .timestamp_ns = commrat::Time::now(),
            .sequence_number = sequence_number_++,
            .sample_rate_hz = this->params_.sample_rate_hz,
            .beats_per_bar = this->params_.beats_per_bar,
            .beat_unit = this->params_.beat_unit,
            .state = this->params_.state,
            .flags = static_cast<uint16_t>(discontinuity_pending_
                ? Messages::TRANSPORT_DISCONTINUITY
                : 0),
        };
        discontinuity_pending_ = false;
        if (this->params_.state == Messages::TRANSPORT_PLAYING
            || this->params_.state == Messages::TRANSPORT_RECORDING) {
            transport_frame_ += frames_per_period_;
            beat_position_ += static_cast<double>(frames_per_period_)
                * this->params_.tempo_bpm
                / (60.0 * static_cast<double>(this->params_.sample_rate_hz));
        }
    }

    void on_params_changed() override {
        validate_params();
        frames_per_period_ = calculate_frame_count(
            period_ms_, this->params_.sample_rate_hz);
        if (this->params_.initial_beat_position != configured_initial_beat_) {
            beat_position_ = this->params_.initial_beat_position;
            configured_initial_beat_ = this->params_.initial_beat_position;
        }
        discontinuity_pending_ = true;
    }

private:
    static uint64_t calculate_frame_count(
        const commrat::ModuleConfig& config,
        uint32_t sample_rate_hz) {
        if (!config.period.has_value() || config.period->count() <= 0) {
            throw std::invalid_argument("TransportSource requires a valid period");
        }
        const auto frames = static_cast<uint64_t>(std::llround(
            static_cast<double>(sample_rate_hz)
            * static_cast<double>(config.period->count()) / 1000.0));
        if (frames == 0) {
            throw std::invalid_argument("TransportSource period is too short");
        }
        return frames;
    }

    static uint64_t calculate_frame_count(
        uint64_t period_ms,
        uint32_t sample_rate_hz) {
        const auto frames = static_cast<uint64_t>(std::llround(
            static_cast<double>(sample_rate_hz)
            * static_cast<double>(period_ms) / 1000.0));
        if (frames == 0) {
            throw std::invalid_argument("TransportSource period is too short");
        }
        return frames;
    }

    void validate_params() const {
        if (!std::isfinite(this->params_.tempo_bpm)
            || this->params_.tempo_bpm <= 0.0
            || !std::isfinite(this->params_.initial_beat_position)
            || this->params_.initial_beat_position < 0.0
            || this->params_.sample_rate_hz == 0
            || this->params_.beats_per_bar == 0
            || this->params_.beat_unit == 0
            || this->params_.state > Messages::TRANSPORT_RECORDING) {
            throw std::invalid_argument("TransportSource parameters are invalid");
        }
    }

    uint64_t period_ms_{0};
    uint64_t frames_per_period_{0};
    uint64_t transport_frame_{0};
    uint64_t sequence_number_{0};
    double beat_position_{0.0};
        double configured_initial_beat_{beat_position_};
    bool discontinuity_pending_{true};
};

} // namespace CommRaT