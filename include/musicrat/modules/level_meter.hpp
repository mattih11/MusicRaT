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
        return musicrat::launcher::audio_passthrough_metadata();
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