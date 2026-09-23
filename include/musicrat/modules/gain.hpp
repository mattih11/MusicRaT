#pragma once

#include <musicrat/dsp/gain_processor.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/gain.hpp>

namespace CommRaT {

class Gain : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::Input<Messages::AudioBlock>,
    commrat::SyncedInput<Messages::ParameterEventBlock>,
    commrat::Params<Parameters::Gain>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::Input<Messages::AudioBlock>,
        commrat::SyncedInput<Messages::ParameterEventBlock>,
        commrat::Params<Parameters::Gain>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        auto metadata = audio_passthrough_metadata();
        metadata.musicrat_ports.ports = {
            {
                .id = "audio_out",
                .display_name = "Audio Out",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_AUDIO,
            },
            {
                .id = "audio_in",
                .display_name = "Audio In",
                .direction = PORT_DIRECTION_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_AUDIO,
            },
            {
                .id = "parameter_events",
                .display_name = "Parameter Events",
                .direction = PORT_DIRECTION_SYNCED_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_PARAMETER,
                .required = false,
            },
        };
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::GAIN_PARAMETER_ID,
                .name = "gain",
                .display_name = "Gain",
                .group = "Level",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "x",
                .minimum = 0.0,
                .maximum = 4.0,
                .step = 0.01,
                .display_scale = PARAMETER_SCALE_DECIBEL,
                .automatable = true,
            },
            {
                .id = Parameters::GAIN_SMOOTHING_PARAMETER_ID,
                .name = "smoothing_samples",
                .display_name = "Smoothing",
                .group = "Response",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "samples",
                .minimum = 0.0,
                .maximum = static_cast<double>(Messages::AudioBlock::MAX_FRAMES),
                .step = 1.0,
            },
            {
                .id = Parameters::GAIN_MUTED_PARAMETER_ID,
                .name = "muted",
                .display_name = "Mute",
                .group = "Level",
                .kind = PARAMETER_KIND_BOOLEAN,
            },
            {
                .id = Parameters::GAIN_INVERT_POLARITY_PARAMETER_ID,
                .name = "invert_polarity",
                .display_name = "Invert Polarity",
                .group = "Level",
                .kind = PARAMETER_KIND_BOOLEAN,
            },
        };
        return metadata;
    }

    explicit Gain(const commrat::ModuleConfig& config)
        : Base(config) {
        processor_.set_parameters_immediate(this->params_);
    }

protected:
    void process(
        const Messages::AudioBlock& input,
        const commrat::Synced<Messages::ParameterEventBlock>& parameter_events,
        Messages::AudioBlock& output) override {
        const Messages::ParameterEventBlock* events = nullptr;
        if (parameter_events.is_fresh()) {
            events = &parameter_events.value();
        }
        processor_.process(input, events, output);
    }

    void on_params_changed() override {
        processor_.set_parameters(this->params_);
    }

private:
    musicrat::dsp::GainProcessor processor_{};
};

} // namespace CommRaT