#pragma once

#include <musicrat/dsp/action_mapper.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

namespace CommRaT {

class ActionMapper : public MusicRaT::Module2<
    commrat::Output<Messages::DeckControlEventBlock>,
    commrat::Input<Messages::ControlEventBlock>,
    commrat::Params<Parameters::ActionMapper>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::DeckControlEventBlock>,
        commrat::Input<Messages::ControlEventBlock>,
        commrat::Params<Parameters::ActionMapper>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {
            {
                .id = "deck_control",
                .display_name = "Deck Control",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_CONTROL,
            },
            {
                .id = "control_events",
                .display_name = "Control Events",
                .direction = PORT_DIRECTION_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_CONTROL,
            },
        };
        return metadata;
    }

    explicit ActionMapper(const commrat::ModuleConfig& config)
        : Base(config) {
        configure();
    }

protected:
    void process(
        const Messages::ControlEventBlock& input,
        Messages::DeckControlEventBlock& output) override {
        if (!configured_) {
            output.events.clear();
            output.timestamp_ns = input.timestamp_ns;
            output.sequence_number = input.sequence_number;
            return;
        }
        (void)mapper_.process(input, musicrat::config::max_audio_frames, output);
    }

    void on_params_changed() override {
        configure();
    }

private:
    void configure() noexcept {
        configured_ = mapper_.configure(
            this->params_.bindings.data(), this->params_.bindings.size())
            == musicrat::dsp::ActionMapperConfigError::None;
    }

    musicrat::dsp::ActionMapper mapper_{};
    bool configured_{false};
};

} // namespace CommRaT
