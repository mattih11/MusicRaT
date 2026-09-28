#pragma once

#include <musicrat/dsp/widget_update_merger.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

namespace CommRaT {

class WidgetUpdateMerger : public MusicRaT::Module2<
    commrat::Output<Messages::WidgetUpdateBlock>,
    commrat::Input<Messages::WidgetUpdateBlock>,
    commrat::SyncedInput<Messages::WidgetUpdateBlock>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::WidgetUpdateBlock>,
        commrat::Input<Messages::WidgetUpdateBlock>,
        commrat::SyncedInput<Messages::WidgetUpdateBlock>>;

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
                .id = "primary_updates",
                .display_name = "Primary Updates",
                .direction = PORT_DIRECTION_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_TELEMETRY,
            },
            {
                .id = "secondary_updates",
                .display_name = "Secondary Updates",
                .direction = PORT_DIRECTION_SYNCED_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_TELEMETRY,
            },
        };
        return metadata;
    }

    explicit WidgetUpdateMerger(const commrat::ModuleConfig& config)
        : Base(config) {}

protected:
    void process(
        const Messages::WidgetUpdateBlock& primary,
        const commrat::Synced<Messages::WidgetUpdateBlock>& secondary,
        Messages::WidgetUpdateBlock& output) override {
        const auto* secondary_block = secondary.is_fresh()
            ? &secondary.value()
            : nullptr;
        (void)merger_.process(primary, secondary_block, output);
    }

private:
    musicrat::dsp::WidgetUpdateMerger merger_{};
};

} // namespace CommRaT