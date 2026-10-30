#pragma once

#include <musicrat/dsp/dc_blocker_processor.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/dc_blocker.hpp>

namespace CommRaT {

class DcBlocker : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::Input<Messages::AudioBlock>,
    commrat::Params<Parameters::DcBlocker>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::Input<Messages::AudioBlock>,
        commrat::Params<Parameters::DcBlocker>>;

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
        metadata.musicrat_parameters.parameters = {{
            .id = Parameters::DC_BLOCKER_CUTOFF_PARAMETER_ID,
            .name = "cutoff_hz",
            .display_name = "Cutoff",
            .group = "Filter",
            .kind = PARAMETER_KIND_CONTINUOUS,
            .unit = "Hz",
            .minimum = 0.1,
            .maximum = 100.0,
            .step = 0.1,
            .choices = {},
        }};
        return metadata;
    }

    explicit DcBlocker(const commrat::ModuleConfig& config)
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
    musicrat::dsp::DcBlockerProcessor processor_{};
};

} // namespace CommRaT
