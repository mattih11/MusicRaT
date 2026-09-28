#pragma once

#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/control_feedback.hpp>
#include <musicrat/protocol/control_events.hpp>

#include <cmath>

namespace CommRaT {

class ControlSource : public MusicRaT::Module2<
    commrat::Output<Messages::ControlEventBlock>,
    commrat::SyncedInput<Messages::ControlFeedbackBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::ControlSource>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::ControlEventBlock>,
        commrat::SyncedInput<Messages::ControlFeedbackBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::ControlSource>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {
            {
                .id = "control_events",
                .display_name = "Control Events",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_CONTROL,
            },
            {
                .id = "control_feedback",
                .display_name = "Control Feedback",
                .direction = PORT_DIRECTION_SYNCED_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_CONTROL,
                .required = false,
            },
        };
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::CONTROL_SOURCE_DEVICE_ID_PARAMETER_ID,
                .name = "device_id",
                .display_name = "Device ID",
                .group = "Endpoint",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "",
                .minimum = 1.0,
                .maximum = 4294967295.0,
                .step = 1.0,
                .choices = {},
            },
            {
                .id = Parameters::CONTROL_SOURCE_ENDPOINT_ID_PARAMETER_ID,
                .name = "endpoint_id",
                .display_name = "Endpoint ID",
                .group = "Endpoint",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "",
                .minimum = 1.0,
                .maximum = 4294967295.0,
                .step = 1.0,
                .choices = {},
            },
            {
                .id = Parameters::CONTROL_SOURCE_ORIGIN_ID_PARAMETER_ID,
                .name = "origin_id",
                .display_name = "Origin ID",
                .group = "Endpoint",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "",
                .minimum = 1.0,
                .maximum = 4294967295.0,
                .step = 1.0,
                .choices = {},
            },
            {
                .id = Parameters::CONTROL_SOURCE_KIND_PARAMETER_ID,
                .name = "kind",
                .display_name = "Kind",
                .group = "Event",
                .kind = PARAMETER_KIND_CHOICE,
                .unit = "",
                .choices = {
                    {.value = Messages::CONTROL_UNIPOLAR, .label = "Unipolar"},
                    {.value = Messages::CONTROL_BIPOLAR, .label = "Bipolar"},
                    {.value = Messages::CONTROL_RELATIVE, .label = "Relative"},
                    {.value = Messages::CONTROL_BOOLEAN, .label = "Boolean"},
                    {.value = Messages::CONTROL_CHOICE, .label = "Choice"},
                    {.value = Messages::CONTROL_GATE, .label = "Gate"},
                    {.value = Messages::CONTROL_TRIGGER, .label = "Trigger"},
                },
            },
            {
                .id = Parameters::CONTROL_SOURCE_VALUE_PARAMETER_ID,
                .name = "value",
                .display_name = "Value",
                .group = "Event",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .unit = "",
                .minimum = -16.0,
                .maximum = 16.0,
                .step = 0.01,
                .choices = {},
            },
            {
                .id = Parameters::CONTROL_SOURCE_GESTURE_FLAGS_PARAMETER_ID,
                .name = "gesture_flags",
                .display_name = "Gesture Flags",
                .group = "Event",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "",
                .minimum = 0.0,
                .maximum = 15.0,
                .step = 1.0,
                .choices = {},
            },
            {
                .id = Parameters::CONTROL_SOURCE_ENABLED_PARAMETER_ID,
                .name = "enabled",
                .display_name = "Enabled",
                .group = "Event",
                .kind = PARAMETER_KIND_BOOLEAN,
                .unit = "",
                .choices = {},
            },
        };
        return metadata;
    }

    explicit ControlSource(const commrat::ModuleConfig& config)
        : Base(config) {
        normalize_params();
    }

    [[nodiscard]] bool has_feedback() const noexcept {
        return has_feedback_;
    }

    [[nodiscard]] double feedback_value() const noexcept {
        return feedback_value_;
    }

protected:
    void process(
        const commrat::Synced<Messages::ControlFeedbackBlock>& feedback,
        Messages::ControlEventBlock& output) override {
        if (feedback.is_fresh()) {
            for (const auto& event : feedback.value().events) {
                if (event.destination_device_id == this->params_.device_id
                    && event.destination_endpoint_id == this->params_.endpoint_id
                    && event.origin_id != this->params_.origin_id
                    && std::isfinite(event.value)) {
                    feedback_value_ = event.value;
                    has_feedback_ = true;
                }
            }
        }
        output.events.clear();
        output.timestamp_ns = commrat::Time::now();
        output.sequence_number = sequence_number_++;
        output.flags = 0;
        if (!this->params_.enabled) return;
        output.events.push_back({
            .source_device_id = this->params_.device_id,
            .source_endpoint_id = this->params_.endpoint_id,
            .origin_id = this->params_.origin_id,
            .sample_offset = 0,
            .kind = this->params_.kind,
            .value = this->params_.value,
            .flags = this->params_.gesture_flags,
        });
    }

    void on_params_changed() override {
        normalize_params();
    }

private:
    void normalize_params() noexcept {
        if (this->params_.device_id == Messages::INVALID_CONTROL_DEVICE_ID) {
            this->params_.device_id = 1;
        }
        if (this->params_.endpoint_id == Messages::INVALID_CONTROL_ENDPOINT_ID) {
            this->params_.endpoint_id = 1;
        }
        if (this->params_.origin_id == Messages::INVALID_CONTROL_ORIGIN_ID) {
            this->params_.origin_id = this->params_.endpoint_id;
        }
        if (this->params_.kind > Messages::CONTROL_TRIGGER) {
            this->params_.kind = Messages::CONTROL_UNIPOLAR;
        }
        if (!std::isfinite(this->params_.value)) {
            this->params_.value = 0.0;
        }
        constexpr uint16_t gesture_mask =
            Messages::CONTROL_EVENT_GESTURE_BEGIN
            | Messages::CONTROL_EVENT_GESTURE_UPDATE
            | Messages::CONTROL_EVENT_GESTURE_END
            | Messages::CONTROL_EVENT_GESTURE_CANCEL;
        this->params_.gesture_flags &= gesture_mask;
    }

    uint64_t sequence_number_{0};
    double feedback_value_{0.0};
    bool has_feedback_{false};
};

} // namespace CommRaT