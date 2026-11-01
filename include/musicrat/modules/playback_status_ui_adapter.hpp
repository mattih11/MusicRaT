#pragma once

#include <musicrat/dsp/playback_status_ui_adapter.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

#include <stdexcept>

namespace CommRaT {

class PlaybackStatusUiAdapter : public MusicRaT::Module2<
    commrat::Output<Messages::WidgetUpdateBlock>,
    commrat::Input<Messages::PlaybackStatusBlock>,
    commrat::Params<Parameters::PlaybackStatusUiAdapter>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::WidgetUpdateBlock>,
        commrat::Input<Messages::PlaybackStatusBlock>,
        commrat::Params<Parameters::PlaybackStatusUiAdapter>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {
            {.id = "widget_updates", .display_name = "Widget Updates",
             .direction = PORT_DIRECTION_OUTPUT, .port_index = 0,
             .domain = PORT_DOMAIN_TELEMETRY},
            {.id = "playback_status", .display_name = "Playback Status",
             .direction = PORT_DIRECTION_INPUT, .port_index = 0,
             .domain = PORT_DOMAIN_TELEMETRY},
        };
        return metadata;
    }

    explicit PlaybackStatusUiAdapter(const commrat::ModuleConfig& config)
        : Base(config) {
        if (configure() != musicrat::dsp::PlaybackStatusUiAdapterConfigError::None) {
            throw std::invalid_argument("Invalid playback UI observation bindings");
        }
    }

protected:
    void process(const Messages::PlaybackStatusBlock& input,
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
        configured_ = configure() == musicrat::dsp::PlaybackStatusUiAdapterConfigError::None;
    }

private:
    musicrat::dsp::PlaybackStatusUiAdapterConfigError configure() noexcept {
        return adapter_.configure(this->params_.bindings.data(), this->params_.bindings.size());
    }

    musicrat::dsp::PlaybackStatusUiAdapter adapter_{};
    bool configured_{true};
};

} // namespace CommRaT
