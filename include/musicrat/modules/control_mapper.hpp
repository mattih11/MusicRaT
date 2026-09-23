#pragma once

#include <musicrat/dsp/control_mapper.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/control_mapping.hpp>

namespace CommRaT {

class ControlMapper : public MusicRaT::Module2<
    commrat::Output<Messages::ParameterEventBlock>,
    commrat::Input<Messages::ControlEventBlock>,
    commrat::SyncedInput<Messages::ParameterStateBlock>,
    commrat::Params<Parameters::ControlMapper>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::ParameterEventBlock>,
        commrat::Input<Messages::ControlEventBlock>,
        commrat::SyncedInput<Messages::ParameterStateBlock>,
        commrat::Params<Parameters::ControlMapper>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {
            {
                .id = "parameter_events",
                .display_name = "Parameter Events",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_PARAMETER,
            },
            {
                .id = "control_events",
                .display_name = "Control Events",
                .direction = PORT_DIRECTION_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_CONTROL,
            },
            {
                .id = "parameter_state",
                .display_name = "Parameter State",
                .direction = PORT_DIRECTION_SYNCED_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_PARAMETER,
                .required = false,
            },
        };
        return metadata;
    }

    explicit ControlMapper(const commrat::ModuleConfig& config)
        : Base(config) {
        configure();
    }

protected:
    void process(
        const Messages::ControlEventBlock& input,
        const commrat::Synced<Messages::ParameterStateBlock>& parameter_state,
        Messages::ParameterEventBlock& output) override {
        if (parameter_state.is_fresh()) {
            (void)mapper_.synchronize(parameter_state.value());
        }
        if (!configured_) {
            output.events.clear();
            output.timestamp_ns = input.timestamp_ns;
            output.sequence_number = input.sequence_number;
            output.flags = Messages::PARAMETER_EVENT_BLOCK_INVALID_CONFIG;
            return;
        }
        (void)mapper_.process(
            input, musicrat::config::max_audio_frames, output);
    }

    void on_params_changed() override {
        configure();
    }

private:
    void configure() noexcept {
        configured_ = mapper_.configure(
            this->params_.bindings.data(),
            this->params_.bindings.size())
            == musicrat::dsp::ControlMapperConfigError::None;
    }

    musicrat::dsp::ControlMapper mapper_{};
    bool configured_{false};
};

} // namespace CommRaT