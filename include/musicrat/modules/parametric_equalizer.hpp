#pragma once

#include <musicrat/dsp/parametric_equalizer_processor.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/parametric_equalizer.hpp>

namespace CommRaT {

class ParametricEqualizer : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::Input<Messages::AudioBlock>,
    commrat::SyncedInput<Messages::ParameterEventBlock>,
    commrat::Params<Parameters::ParametricEqualizer>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::Input<Messages::AudioBlock>,
        commrat::SyncedInput<Messages::ParameterEventBlock>,
        commrat::Params<Parameters::ParametricEqualizer>>;

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
                .id = Parameters::PARAMETRIC_EQ_LOW_FREQUENCY_PARAMETER_ID,
                .name = "low_shelf_frequency_hz",
                .display_name = "Low Shelf Frequency",
                .group = "Low Shelf",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "Hz",
                .minimum = 1.0,
                .maximum = 20000.0,
                .step = 1.0,
                .automatable = true,
                .choices = {},
            },
            {
                .id = Parameters::PARAMETRIC_EQ_LOW_GAIN_PARAMETER_ID,
                .name = "low_shelf_gain_db",
                .display_name = "Low Shelf Gain",
                .group = "Low Shelf",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "dB",
                .minimum = -24.0,
                .maximum = 24.0,
                .step = 0.1,
                .automatable = true,
                .choices = {},
            },
            {
                .id = Parameters::PARAMETRIC_EQ_MID_FREQUENCY_PARAMETER_ID,
                .name = "mid_frequency_hz",
                .display_name = "Mid Frequency",
                .group = "Peaking Mid",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "Hz",
                .minimum = 1.0,
                .maximum = 20000.0,
                .step = 1.0,
                .automatable = true,
                .choices = {},
            },
            {
                .id = Parameters::PARAMETRIC_EQ_MID_GAIN_PARAMETER_ID,
                .name = "mid_gain_db",
                .display_name = "Mid Gain",
                .group = "Peaking Mid",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "dB",
                .minimum = -24.0,
                .maximum = 24.0,
                .step = 0.1,
                .automatable = true,
                .choices = {},
            },
            {
                .id = Parameters::PARAMETRIC_EQ_MID_Q_PARAMETER_ID,
                .name = "mid_q",
                .display_name = "Mid Q",
                .group = "Peaking Mid",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "Q",
                .minimum = 0.1,
                .maximum = 20.0,
                .step = 0.01,
                .automatable = true,
                .choices = {},
            },
            {
                .id = Parameters::PARAMETRIC_EQ_HIGH_FREQUENCY_PARAMETER_ID,
                .name = "high_shelf_frequency_hz",
                .display_name = "High Shelf Frequency",
                .group = "High Shelf",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "Hz",
                .minimum = 1.0,
                .maximum = 20000.0,
                .step = 1.0,
                .automatable = true,
                .choices = {},
            },
            {
                .id = Parameters::PARAMETRIC_EQ_HIGH_GAIN_PARAMETER_ID,
                .name = "high_shelf_gain_db",
                .display_name = "High Shelf Gain",
                .group = "High Shelf",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "dB",
                .minimum = -24.0,
                .maximum = 24.0,
                .step = 0.1,
                .automatable = true,
                .choices = {},
            },
        };
        return metadata;
    }

    explicit ParametricEqualizer(const commrat::ModuleConfig& config)
        : Base(config)
        , processor_(this->params_) {}

protected:
    void process(
        const Messages::AudioBlock& input,
        const commrat::Synced<Messages::ParameterEventBlock>& parameter_events,
        Messages::AudioBlock& output) override {
        const Messages::ParameterEventBlock* events =
            parameter_events.is_fresh() ? &parameter_events.value() : nullptr;
        processor_.process(input, events, output);
    }

    void on_params_changed() override {
        processor_.set_parameters(this->params_);
    }

private:
    musicrat::dsp::ParametricEqualizerProcessor processor_{};
};

} // namespace CommRaT
