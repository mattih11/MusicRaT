#pragma once

#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/parameter_events.hpp>

#include <cmath>

namespace CommRaT {

class ParameterSource : public MusicRaT::Module2<
    commrat::Output<Messages::ParameterEventBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::ParameterSource>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::ParameterEventBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::ParameterSource>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {{
            .id = "parameter_events",
            .display_name = "Parameter Events",
            .direction = PORT_DIRECTION_OUTPUT,
            .port_index = 0,
            .domain = PORT_DOMAIN_PARAMETER,
        }};
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::PARAMETER_SOURCE_ENDPOINT_ID_PARAMETER_ID,
                .name = "source_endpoint_id",
                .display_name = "Source Endpoint ID",
                .group = "Event",
                .kind = PARAMETER_KIND_INTEGER,
                .minimum = 1.0,
                .maximum = 4294967295.0,
                .step = 1.0,
            },
            {
                .id = Parameters::PARAMETER_SOURCE_PARAMETER_ID_PARAMETER_ID,
                .name = "parameter_id",
                .display_name = "Parameter ID",
                .group = "Event",
                .kind = PARAMETER_KIND_INTEGER,
                .minimum = 1.0,
                .maximum = 4294967295.0,
                .step = 1.0,
            },
            {
                .id = Parameters::PARAMETER_SOURCE_VALUE_PARAMETER_ID,
                .name = "value",
                .display_name = "Value",
                .group = "Event",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .minimum = -16.0,
                .maximum = 16.0,
                .step = 0.01,
            },
        };
        return metadata;
    }

    explicit ParameterSource(const commrat::ModuleConfig& config)
        : Base(config) {
        normalize_params();
    }

protected:
    void process(Messages::ParameterEventBlock& output) override {
        output.events.clear();
        output.timestamp_ns = commrat::Time::now();
        output.sequence_number = sequence_number_++;
        output.flags = 0;
        output.events.push_back(Messages::ParameterEvent{
            .source_endpoint_id = this->params_.source_endpoint_id,
            .parameter_id = this->params_.parameter_id,
            .sample_offset = 0,
            .value = this->params_.value,
        });
    }

    void on_params_changed() override {
        normalize_params();
    }

private:
    void normalize_params() noexcept {
        if (!std::isfinite(this->params_.value)) {
            this->params_.value = 0.0;
        }
    }

    uint64_t sequence_number_{0};
};

} // namespace CommRaT