#pragma once

#include <musicrat/dsp/wavetable_oscillator_processor.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/modules/note_oscillator_support.hpp>
#include <musicrat/protocol/wavetable_oscillator.hpp>

#include <algorithm>
#include <cmath>

namespace CommRaT {

class WavetableOscillator : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::SyncedInput<Messages::NoteEventBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::WavetableOscillator>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::SyncedInput<Messages::NoteEventBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::WavetableOscillator>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        auto metadata = audio_source_metadata("sample_rate_hz", 1);
        metadata.musicrat_ports.ports = {
            {
                .id = "audio_out",
                .display_name = "Audio Out",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_AUDIO,
            },
            {
                .id = "note_events",
                .display_name = "Note Events",
                .direction = PORT_DIRECTION_SYNCED_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_NOTE,
            },
        };
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::WAVETABLE_OSCILLATOR_SAMPLE_RATE_PARAMETER_ID,
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
                .id = Parameters::WAVETABLE_OSCILLATOR_WAVEFORM_PARAMETER_ID,
                .name = "waveform",
                .display_name = "Wavetable",
                .group = "Oscillator",
                .kind = PARAMETER_KIND_CHOICE,
                .unit = "",
                .choices = {
                    {.value = Parameters::SYNTH_WAVEFORM_SINE, .label = "Sine"},
                    {.value = Parameters::SYNTH_WAVEFORM_SQUARE, .label = "Square"},
                    {.value = Parameters::SYNTH_WAVEFORM_TRIANGLE, .label = "Triangle"},
                    {.value = Parameters::SYNTH_WAVEFORM_SAWTOOTH, .label = "Sawtooth"},
                    {.value = Parameters::SYNTH_WAVEFORM_REVERSE_SAWTOOTH,
                        .label = "Reverse Sawtooth"},
                },
            },
            {
                .id = Parameters::WAVETABLE_OSCILLATOR_GAIN_PARAMETER_ID,
                .name = "gain",
                .display_name = "Gain",
                .group = "Output",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "x",
                .minimum = 0.0,
                .maximum = 1.0,
                .step = 0.01,
                .choices = {},
            },
            {
                .id = Parameters::WAVETABLE_OSCILLATOR_ENVELOPE_PARAMETER_ID,
                .name = "envelope_ms",
                .display_name = "Attack/Release",
                .group = "Notes",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "ms",
                .minimum = 1.0,
                .maximum = 100.0,
                .step = 1.0,
                .choices = {},
            },
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_AMP_ATTACK_PARAMETER_ID,
                "amplitude_attack_ms", "Attack", "Amplitude Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_AMP_DECAY_PARAMETER_ID,
                "amplitude_decay_ms", "Decay", "Amplitude Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_AMP_SUSTAIN_PARAMETER_ID,
                "amplitude_sustain_level", "Sustain", "Amplitude Envelope",
                "x", 0.0, 1.0, 0.01),
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_AMP_RELEASE_PARAMETER_ID,
                "amplitude_release_ms", "Release", "Amplitude Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_filter_mode(
                Parameters::WAVETABLE_OSCILLATOR_FILTER_MODE_PARAMETER_ID),
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_FILTER_CUTOFF_PARAMETER_ID,
                "filter_cutoff_hz", "Cutoff", "Filter",
                "Hz", 1.0, 20000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_FILTER_RESONANCE_PARAMETER_ID,
                "filter_resonance", "Resonance", "Filter",
                "Q", 0.1, 20.0, 0.01),
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_FILTER_ATTACK_PARAMETER_ID,
                "filter_attack_ms", "Attack", "Filter Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_FILTER_DECAY_PARAMETER_ID,
                "filter_decay_ms", "Decay", "Filter Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_FILTER_SUSTAIN_PARAMETER_ID,
                "filter_sustain_level", "Sustain", "Filter Envelope",
                "x", 0.0, 1.0, 0.01),
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_FILTER_RELEASE_PARAMETER_ID,
                "filter_release_ms", "Release", "Filter Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::WAVETABLE_OSCILLATOR_FILTER_ENV_AMOUNT_PARAMETER_ID,
                "filter_env_amount_octaves", "Envelope Amount",
                "Filter Envelope", "octaves", -4.0, 4.0, 0.01),
        };
        return metadata;
    }

    explicit WavetableOscillator(const commrat::ModuleConfig& config)
        : Base(config) {
        normalize_params();
        frame_count_ = ModuleDetail::calculate_note_oscillator_frame_count(
            config, this->params_.sample_rate_hz);
        processor_.set_parameters(this->params_);
    }

protected:
    void process(
        const commrat::Synced<Messages::NoteEventBlock>& note_events,
        Messages::AudioBlock& output) override {
        const auto* events = note_events.is_fresh()
            ? &note_events.value()
            : nullptr;
        processor_.process(
            events, static_cast<uint32_t>(frame_count_),
            commrat::Time::now(), sequence_number_++, output);
    }

    void on_params_changed() override {
        normalize_params();
        frame_count_ = ModuleDetail::calculate_note_oscillator_frame_count(
            this->config_, this->params_.sample_rate_hz);
        processor_.set_parameters(this->params_);
    }

private:
    void normalize_params() {
        if (!std::isfinite(this->params_.sample_rate_hz)) {
            this->params_.sample_rate_hz =
                musicrat::config::default_sample_rate_hz;
        }
        this->params_.sample_rate_hz = std::clamp(
            this->params_.sample_rate_hz, 8000.0, 192000.0);
    }

    musicrat::dsp::WavetableOscillatorProcessor processor_{};
    std::size_t frame_count_{0};
    uint64_t sequence_number_{0};
};

} // namespace CommRaT
