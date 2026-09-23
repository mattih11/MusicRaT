#pragma once

#include <musicrat/dsp/level_meter_processor.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

namespace CommRaT {

class LevelMeter : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::Output<Messages::LevelMeterBlock>,
    commrat::Input<Messages::AudioBlock>,
    commrat::Params<Parameters::LevelMeter>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::Output<Messages::LevelMeterBlock>,
        commrat::Input<Messages::AudioBlock>,
        commrat::Params<Parameters::LevelMeter>>;

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
                .id = "levels",
                .display_name = "Levels",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 1,
                .domain = PORT_DOMAIN_TELEMETRY,
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
            .id = Parameters::LEVEL_METER_CLIPPING_THRESHOLD_PARAMETER_ID,
            .name = "clipping_threshold",
            .display_name = "Clipping Threshold",
            .group = "Meter",
            .kind = PARAMETER_KIND_CONTINUOUS,
            .minimum = 0.0001,
            .maximum = 4.0,
            .step = 0.01,
        }};
        return metadata;
    }

    explicit LevelMeter(const commrat::ModuleConfig& config)
        : Base(config)
        , processor_(this->params_) {}

protected:
    void process(
        const Messages::AudioBlock& input,
        Messages::AudioBlock& output,
        Messages::LevelMeterBlock& meter) override {
        processor_.process(input, output, meter);
    }

    void on_params_changed() override {
        processor_.set_parameters(this->params_);
    }

private:
    musicrat::dsp::LevelMeterProcessor processor_{};
};

} // namespace CommRaT