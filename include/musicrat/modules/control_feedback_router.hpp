#pragma once

#include <musicrat/dsp/control_feedback_router.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

namespace CommRaT {

class ControlFeedbackRouter : public MusicRaT::Module2<
    commrat::Output<Messages::ControlFeedbackBlock>,
    commrat::Input<Messages::ParameterStateBlock>,
    commrat::Params<Parameters::ControlFeedbackRouter>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::ControlFeedbackBlock>,
        commrat::Input<Messages::ParameterStateBlock>,
        commrat::Params<Parameters::ControlFeedbackRouter>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {
            {
                .id = "control_feedback",
                .display_name = "Control Feedback",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_CONTROL,
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

    explicit ControlFeedbackRouter(const commrat::ModuleConfig& config)
        : Base(config) {
        configure();
    }

protected:
    void process(
        const Messages::ParameterStateBlock& input,
        Messages::ControlFeedbackBlock& output) override {
        if (!configured_) {
            output.events.clear();
            output.timestamp_ns = input.timestamp_ns;
            output.sequence_number = input.sequence_number;
            output.flags = Messages::CONTROL_FEEDBACK_BLOCK_INVALID_CONFIG;
            return;
        }
        (void)router_.process(input, output);
    }

    void on_params_changed() override {
        configure();
    }

private:
    void configure() noexcept {
        configured_ = router_.configure(
            this->params_.bindings.data(),
            this->params_.bindings.size())
            == musicrat::dsp::ControlFeedbackRouterConfigError::None;
    }

    musicrat::dsp::ControlFeedbackRouter router_{};
    bool configured_{false};
};

} // namespace CommRaT
