#pragma once

#include <musicrat/dsp/biquad_filter_processor.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/biquad_filter.hpp>

namespace CommRaT {

class BiquadFilter : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::Input<Messages::AudioBlock>,
    commrat::Params<Parameters::BiquadFilter>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::Input<Messages::AudioBlock>,
        commrat::Params<Parameters::BiquadFilter>>;

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
        };
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::BIQUAD_FILTER_TYPE_PARAMETER_ID,
                .name = "filter_type",
                .display_name = "Filter Type",
                .group = "Filter",
                .kind = PARAMETER_KIND_CHOICE,
                .unit = "",
                .choices = {
                    {.value = Parameters::BIQUAD_FILTER_LOW_PASS, .label = "Low Pass"},
                    {.value = Parameters::BIQUAD_FILTER_HIGH_PASS, .label = "High Pass"},
                    {.value = Parameters::BIQUAD_FILTER_BAND_PASS, .label = "Band Pass"},
                    {.value = Parameters::BIQUAD_FILTER_NOTCH, .label = "Notch"},
                    {.value = Parameters::BIQUAD_FILTER_PEAKING, .label = "Peaking"},
                    {.value = Parameters::BIQUAD_FILTER_LOW_SHELF, .label = "Low Shelf"},
                    {.value = Parameters::BIQUAD_FILTER_HIGH_SHELF, .label = "High Shelf"},
                },
            },
            {
                .id = Parameters::BIQUAD_FILTER_FREQUENCY_PARAMETER_ID,
                .name = "frequency_hz",
                .display_name = "Frequency",
                .group = "Filter",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "Hz",
                .minimum = 1.0,
                .maximum = 20000.0,
                .step = 1.0,
                .choices = {},
            },
            {
                .id = Parameters::BIQUAD_FILTER_Q_PARAMETER_ID,
                .name = "q",
                .display_name = "Q",
                .group = "Filter",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "Q",
                .minimum = 0.1,
                .maximum = 20.0,
                .step = 0.01,
                .choices = {},
            },
            {
                .id = Parameters::BIQUAD_FILTER_GAIN_PARAMETER_ID,
                .name = "gain_db",
                .display_name = "Gain",
                .group = "Filter",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "dB",
                .minimum = -24.0,
                .maximum = 24.0,
                .step = 0.1,
                .choices = {},
            },
        };
        return metadata;
    }

    explicit BiquadFilter(const commrat::ModuleConfig& config)
        : Base(config)
        , processor_(this->params_) {}

protected:
    void process(
        const Messages::AudioBlock& input,
        Messages::AudioBlock& output) override {
        processor_.process(input, output);
    }

    void on_params_changed() override {
        processor_.set_parameters(this->params_);
    }

private:
    musicrat::dsp::BiquadFilterProcessor processor_{};
};

} // namespace CommRaT
