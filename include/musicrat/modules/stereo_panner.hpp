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
        return musicrat::launcher::audio_channel_transform_metadata(1, 2);
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