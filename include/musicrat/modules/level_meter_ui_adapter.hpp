#pragma once

#include <musicrat/dsp/level_meter_ui_adapter.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

namespace CommRaT {

class LevelMeterUiAdapter : public MusicRaT::Module2<
    commrat::Output<Messages::WidgetUpdateBlock>,
    commrat::Input<Messages::LevelMeterBlock>,
    commrat::Params<Parameters::LevelMeterUiAdapter>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::WidgetUpdateBlock>,
        commrat::Input<Messages::LevelMeterBlock>,
        commrat::Params<Parameters::LevelMeterUiAdapter>>;

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
                .id = "levels",
                .display_name = "Levels",
                .direction = PORT_DIRECTION_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_TELEMETRY,
            },
        };
        return metadata;
    }

    explicit LevelMeterUiAdapter(const commrat::ModuleConfig& config)
        : Base(config) {
        configure();
    }

protected:
    void process(
        const Messages::LevelMeterBlock& input,
        Messages::WidgetUpdateBlock& output) override {
        if (!configured_) {
            output.events.clear();
            output.timestamp_ns = input.timestamp_ns;
            output.sequence_number = input.sequence_number;
            output.flags = static_cast<uint16_t>(
                Messages::WIDGET_UPDATE_BLOCK_SOURCE_INVALID);
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
            == musicrat::dsp::LevelMeterUiAdapterConfigError::None;
    }

    musicrat::dsp::LevelMeterUiAdapter adapter_{};
    bool configured_{false};
};

} // namespace CommRaT
