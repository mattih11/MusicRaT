#pragma once

#include <musicrat/dsp/three_oscillator_processor.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/modules/note_oscillator_support.hpp>
#include <musicrat/protocol/three_oscillator.hpp>
#include <musicrat/protocol/parameter_state.hpp>

#include <algorithm>
#include <cmath>

namespace CommRaT {

class ThreeOscillator : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::Output<Messages::ParameterStateBlock>,
    commrat::SyncedInput<Messages::NoteEventBlock>,
    commrat::SyncedInput<Messages::ParameterEventBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::ThreeOscillator>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::Output<Messages::ParameterStateBlock>,
        commrat::SyncedInput<Messages::NoteEventBlock>,
        commrat::SyncedInput<Messages::ParameterEventBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::ThreeOscillator>>;

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
                .id = "parameter_state",
                .display_name = "Parameter State",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 1,
                .domain = PORT_DOMAIN_PARAMETER,
            },
            {
                .id = "parameter_events",
                .display_name = "Parameter Events",
                .direction = PORT_DIRECTION_SYNCED_INPUT,
                .port_index = 1,
                .domain = PORT_DOMAIN_PARAMETER,
                .required = false,
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
                .id = Parameters::THREE_OSCILLATOR_SAMPLE_RATE_PARAMETER_ID,
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
                .id = Parameters::THREE_OSCILLATOR_MASTER_GAIN_PARAMETER_ID,
                .name = "master_gain",
                .display_name = "Master Gain",
                .group = "Output",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "x",
                .minimum = 0.0,
                .maximum = 1.0,
                .step = 0.01,
                .choices = {},
            },
            {
                .id = Parameters::THREE_OSCILLATOR_ENVELOPE_PARAMETER_ID,
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
            oscillator_waveform_parameter(
                Parameters::THREE_OSCILLATOR_1_WAVEFORM_PARAMETER_ID,
                "oscillator_1_waveform", "Oscillator 1 Waveform", "Oscillator 1"),
            oscillator_semitones_parameter(
                Parameters::THREE_OSCILLATOR_1_SEMITONES_PARAMETER_ID,
                "oscillator_1_semitones", "Oscillator 1 Semitones", "Oscillator 1"),
            oscillator_level_parameter(
                Parameters::THREE_OSCILLATOR_1_LEVEL_PARAMETER_ID,
                "oscillator_1_level", "Oscillator 1 Level", "Oscillator 1"),
            oscillator_waveform_parameter(
                Parameters::THREE_OSCILLATOR_2_WAVEFORM_PARAMETER_ID,
                "oscillator_2_waveform", "Oscillator 2 Waveform", "Oscillator 2"),
            oscillator_semitones_parameter(
                Parameters::THREE_OSCILLATOR_2_SEMITONES_PARAMETER_ID,
                "oscillator_2_semitones", "Oscillator 2 Semitones", "Oscillator 2"),
            oscillator_level_parameter(
                Parameters::THREE_OSCILLATOR_2_LEVEL_PARAMETER_ID,
                "oscillator_2_level", "Oscillator 2 Level", "Oscillator 2"),
            oscillator_waveform_parameter(
                Parameters::THREE_OSCILLATOR_3_WAVEFORM_PARAMETER_ID,
                "oscillator_3_waveform", "Oscillator 3 Waveform", "Oscillator 3"),
            oscillator_semitones_parameter(
                Parameters::THREE_OSCILLATOR_3_SEMITONES_PARAMETER_ID,
                "oscillator_3_semitones", "Oscillator 3 Semitones", "Oscillator 3"),
            oscillator_level_parameter(
                Parameters::THREE_OSCILLATOR_3_LEVEL_PARAMETER_ID,
                "oscillator_3_level", "Oscillator 3 Level", "Oscillator 3"),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_AMP_ATTACK_PARAMETER_ID,
                "amplitude_attack_ms", "Attack", "Amplitude Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_AMP_DECAY_PARAMETER_ID,
                "amplitude_decay_ms", "Decay", "Amplitude Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_AMP_SUSTAIN_PARAMETER_ID,
                "amplitude_sustain_level", "Sustain", "Amplitude Envelope",
                "x", 0.0, 1.0, 0.01),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_AMP_RELEASE_PARAMETER_ID,
                "amplitude_release_ms", "Release", "Amplitude Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_filter_mode(
                Parameters::THREE_OSCILLATOR_FILTER_MODE_PARAMETER_ID),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_FILTER_CUTOFF_PARAMETER_ID,
                "filter_cutoff_hz", "Cutoff", "Filter",
                "Hz", 1.0, 20000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_FILTER_RESONANCE_PARAMETER_ID,
                "filter_resonance", "Resonance", "Filter",
                "Q", 0.1, 20.0, 0.01),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_FILTER_ATTACK_PARAMETER_ID,
                "filter_attack_ms", "Attack", "Filter Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_FILTER_DECAY_PARAMETER_ID,
                "filter_decay_ms", "Decay", "Filter Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_FILTER_SUSTAIN_PARAMETER_ID,
                "filter_sustain_level", "Sustain", "Filter Envelope",
                "x", 0.0, 1.0, 0.01),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_FILTER_RELEASE_PARAMETER_ID,
                "filter_release_ms", "Release", "Filter Envelope",
                "ms", 0.0, 10000.0, 1.0),
            ModuleDetail::note_oscillator_parameter(
                Parameters::THREE_OSCILLATOR_FILTER_ENV_AMOUNT_PARAMETER_ID,
                "filter_env_amount_octaves", "Envelope Amount",
                "Filter Envelope", "octaves", -4.0, 4.0, 0.01),
        };
        for (auto& parameter : metadata.musicrat_parameters.parameters) {
            parameter.automatable = parameter.id != Parameters::THREE_OSCILLATOR_SAMPLE_RATE_PARAMETER_ID;
        }
        return metadata;
    }

    explicit ThreeOscillator(const commrat::ModuleConfig& config)
        : Base(config) {
        normalize_params();
        frame_count_ = ModuleDetail::calculate_note_oscillator_frame_count(
            config, this->params_.sample_rate_hz);
        processor_.set_parameters(this->params_);
    }

protected:
    void process(
        const commrat::Synced<Messages::NoteEventBlock>& note_events,
        const commrat::Synced<Messages::ParameterEventBlock>& parameter_events,
        Messages::AudioBlock& output,
        Messages::ParameterStateBlock& parameter_state) override {
        const auto* events = note_events.is_fresh()
            ? &note_events.value()
            : nullptr;
        processor_.process(
            events, parameter_events.is_fresh() ? &parameter_events.value() : nullptr,
            static_cast<uint32_t>(frame_count_),
            commrat::Time::now(), sequence_number_++, output);
        parameter_state.states.clear();
        parameter_state.timestamp_ns = output.timestamp_ns;
        parameter_state.sequence_number = output.sequence_number;
        parameter_state.flags = Messages::PARAMETER_STATE_BLOCK_SNAPSHOT;
        musicrat::dsp::visit_three_oscillator_parameters([&](uint32_t id, auto member) {
            if (parameter_state.states.size() < Messages::ParameterStateBlock::MAX_STATES) {
                parameter_state.states.push_back({
                    .parameter_id = id, .value = static_cast<double>(processor_.parameters().*member),
                });
            } else {
                parameter_state.flags |= Messages::PARAMETER_STATE_BLOCK_OVERFLOW;
            }
        });
    }

    void on_params_changed() override {
        normalize_params();
        frame_count_ = ModuleDetail::calculate_note_oscillator_frame_count(
            this->config_, this->params_.sample_rate_hz);
        processor_.set_parameters(this->params_);
    }

private:
    using ParameterDescriptor = musicrat::launcher::ParameterDescriptor;

    static ParameterDescriptor oscillator_waveform_parameter(
        uint32_t id,
        const char* name,
        const char* display_name,
        const char* group) {
        return {
            .id = id,
            .name = name,
            .display_name = display_name,
            .group = group,
            .kind = musicrat::launcher::PARAMETER_KIND_CHOICE,
            .unit = "",
            .choices = {
                {.value = Parameters::SYNTH_WAVEFORM_SINE, .label = "Sine"},
                {.value = Parameters::SYNTH_WAVEFORM_SQUARE, .label = "Square"},
                {.value = Parameters::SYNTH_WAVEFORM_TRIANGLE, .label = "Triangle"},
                {.value = Parameters::SYNTH_WAVEFORM_SAWTOOTH, .label = "Sawtooth"},
                {.value = Parameters::SYNTH_WAVEFORM_REVERSE_SAWTOOTH,
                    .label = "Reverse Sawtooth"},
            },
        };
    }

    static ParameterDescriptor oscillator_semitones_parameter(
        uint32_t id,
        const char* name,
        const char* display_name,
        const char* group) {
        return {
            .id = id,
            .name = name,
            .display_name = display_name,
            .group = group,
            .kind = musicrat::launcher::PARAMETER_KIND_CONTINUOUS,
            .unit = "semitones",
            .minimum = -48.0,
            .maximum = 48.0,
            .step = 0.1,
            .choices = {},
        };
    }

    static ParameterDescriptor oscillator_level_parameter(
        uint32_t id,
        const char* name,
        const char* display_name,
        const char* group) {
        return {
            .id = id,
            .name = name,
            .display_name = display_name,
            .group = group,
            .kind = musicrat::launcher::PARAMETER_KIND_CONTINUOUS,
            .unit = "x",
            .minimum = 0.0,
            .maximum = 1.0,
            .step = 0.01,
            .choices = {},
        };
    }

    void normalize_params() {
        if (!std::isfinite(this->params_.sample_rate_hz)) {
            this->params_.sample_rate_hz =
                musicrat::config::default_sample_rate_hz;
        }
        this->params_.sample_rate_hz = std::clamp(
            this->params_.sample_rate_hz, 8000.0, 192000.0);
    }

    musicrat::dsp::ThreeOscillatorProcessor processor_{};
    std::size_t frame_count_{0};
    uint64_t sequence_number_{0};
};

} // namespace CommRaT
