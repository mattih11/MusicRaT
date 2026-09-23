#pragma once

#include <musicrat/dsp/stereo_panner_processor.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/stereo_panner.hpp>

namespace CommRaT {

class StereoPanner : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::Input<Messages::AudioBlock>,
    commrat::SyncedInput<Messages::ParameterEventBlock>,
    commrat::Params<Parameters::StereoPanner>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::Input<Messages::AudioBlock>,
        commrat::SyncedInput<Messages::ParameterEventBlock>,
        commrat::Params<Parameters::StereoPanner>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        auto metadata = audio_channel_transform_metadata(1, 2);
        metadata.musicrat_ports.ports = {
            {
                .id = "audio_out",
                .display_name = "Stereo Out",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_AUDIO,
            },
            {
                .id = "audio_in",
                .display_name = "Mono In",
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
                .id = Parameters::PAN_PARAMETER_ID,
                .name = "pan",
                .display_name = "Pan",
                .group = "Position",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .minimum = -1.0,
                .maximum = 1.0,
                .step = 0.01,
                .automatable = true,
            },
            {
                .id = Parameters::PAN_LAW_PARAMETER_ID,
                .name = "pan_law",
                .display_name = "Pan Law",
                .group = "Position",
                .kind = PARAMETER_KIND_CHOICE,
                .choices = {
                    {.value = Parameters::PAN_LAW_LINEAR, .label = "Linear"},
                    {
                        .value = Parameters::PAN_LAW_EQUAL_POWER,
                        .label = "Equal Power",
                    },
                },
            },
            {
                .id = Parameters::PAN_SMOOTHING_PARAMETER_ID,
                .name = "smoothing_samples",
                .display_name = "Smoothing",
                .group = "Response",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "samples",
                .minimum = 0.0,
                .maximum = static_cast<double>(Messages::AudioBlock::MAX_FRAMES),
                .step = 1.0,
            },
        };
        return metadata;
    }

    explicit StereoPanner(const commrat::ModuleConfig& config)
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
    musicrat::dsp::StereoPannerProcessor processor_{};
};

} // namespace CommRaT