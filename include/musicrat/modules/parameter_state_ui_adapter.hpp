#pragma once

#include <musicrat/dsp/parameter_state_ui_adapter.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

namespace CommRaT {

class ParameterStateUiAdapter : public MusicRaT::Module2<
    commrat::Output<Messages::WidgetUpdateBlock>,
    commrat::Input<Messages::ParameterStateBlock>,
    commrat::Params<Parameters::ParameterStateUiAdapter>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::WidgetUpdateBlock>,
        commrat::Input<Messages::ParameterStateBlock>,
        commrat::Params<Parameters::ParameterStateUiAdapter>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {
            {
                .id = "widget_updates",
                .display_name = "Widget Updates",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_TELEMETRY,
            },
            {
                .id = "parameter_state",
                .display_name = "Parameter State",
                .direction = PORT_DIRECTION_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_PARAMETER,
            },
        };
        return metadata;
    }

    explicit ParameterStateUiAdapter(const commrat::ModuleConfig& config)
        : Base(config) {
        configure();
    }

protected:
    void process(
        const Messages::ParameterStateBlock& input,
        Messages::WidgetUpdateBlock& output) override {
        if (!configured_) {
            output.events.clear();
            output.timestamp_ns = input.timestamp_ns;
            output.sequence_number = input.sequence_number;
            output.flags = Messages::WIDGET_UPDATE_BLOCK_SOURCE_INVALID;
            return;
        }
        (void)adapter_.process(input, output);
    }

    void on_params_changed() override {
        configure();
    }

private:
    void configure() noexcept {
        configured_ = adapter_.configure(
            this->params_.bindings.data(), this->params_.bindings.size())
            == musicrat::dsp::ParameterStateUiAdapterConfigError::None;
    }

    musicrat::dsp::ParameterStateUiAdapter adapter_{};
    bool configured_{false};
};

} // namespace CommRaT
