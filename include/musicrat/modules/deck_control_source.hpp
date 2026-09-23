#pragma once

#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

#include <cmath>

namespace CommRaT {

class DeckControlSource : public MusicRaT::Module2<
    commrat::Output<Messages::DeckControlEventBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::DeckControlSource>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::DeckControlEventBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::DeckControlSource>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {{
            .id = "deck_control",
            .display_name = "Deck Control",
            .direction = PORT_DIRECTION_OUTPUT,
            .port_index = 0,
            .domain = PORT_DOMAIN_CONTROL,
        }};
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::DECK_CONTROL_TYPE_PARAMETER_ID,
                .name = "type",
                .display_name = "Action",
                .group = "Control",
                .kind = PARAMETER_KIND_CHOICE,
                .choices = {
                    {.value = Messages::DECK_CONTROL_PLAY, .label = "Play"},
                    {.value = Messages::DECK_CONTROL_PAUSE, .label = "Pause"},
                    {.value = Messages::DECK_CONTROL_SET_RATE, .label = "Set Rate"},
                    {.value = Messages::DECK_CONTROL_RAMP_RATE, .label = "Ramp Rate"},
                    {.value = Messages::DECK_CONTROL_SET_CUE, .label = "Set Cue"},
                    {.value = Messages::DECK_CONTROL_RETURN_TO_CUE, .label = "Return to Cue"},
                    {.value = Messages::DECK_CONTROL_CLEAR_CUE, .label = "Clear Cue"},
                    {.value = Messages::DECK_CONTROL_SET_LOOP_START, .label = "Set Loop Start"},
                    {.value = Messages::DECK_CONTROL_SET_LOOP_END, .label = "Set Loop End"},
                    {.value = Messages::DECK_CONTROL_ENABLE_LOOP, .label = "Enable Loop"},
                    {.value = Messages::DECK_CONTROL_DISABLE_LOOP, .label = "Disable Loop"},
                },
            },
            {
                .id = Parameters::DECK_CONTROL_VALUE_PARAMETER_ID,
                .name = "value",
                .display_name = "Value",
                .group = "Control",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .minimum = -4.0,
                .maximum = 4.0,
                .step = 0.01,
            },
            {
                .id = Parameters::DECK_CONTROL_RAMP_FRAMES_PARAMETER_ID,
                .name = "ramp_frames",
                .display_name = "Ramp",
                .group = "Control",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "frames",
                .minimum = 0.0,
                .maximum = 4294967295.0,
                .step = 1.0,
            },
            {
                .id = Parameters::DECK_CONTROL_QUANTIZATION_PARAMETER_ID,
                .name = "quantization",
                .display_name = "Quantization",
                .group = "Timing",
                .kind = PARAMETER_KIND_CHOICE,
                .choices = {
                    {.value = Messages::DECK_QUANTIZE_IMMEDIATE, .label = "Immediate"},
                    {.value = Messages::DECK_QUANTIZE_BEAT, .label = "Beat"},
                    {.value = Messages::DECK_QUANTIZE_BAR, .label = "Bar"},
                },
            },
            {
                .id = Parameters::DECK_CONTROL_ENABLED_PARAMETER_ID,
                .name = "enabled",
                .display_name = "Enabled",
                .group = "Control",
                .kind = PARAMETER_KIND_BOOLEAN,
            },
        };
        return metadata;
    }

    explicit DeckControlSource(const commrat::ModuleConfig& config)
        : Base(config) {
        normalize_params();
    }

protected:
    void process(Messages::DeckControlEventBlock& output) override {
        output.events.clear();
        output.timestamp_ns = commrat::Time::now();
        output.sequence_number = sequence_number_++;
        if (this->params_.enabled) {
            output.events.push_back(Messages::DeckControlEvent{
                .type = this->params_.type,
                .sample_offset = 0,
                .value = this->params_.value,
                .ramp_frames = this->params_.ramp_frames,
                .quantization = this->params_.quantization,
            });
        }
    }

    void on_params_changed() override {
        normalize_params();
    }

private:
    void normalize_params() noexcept {
        if (this->params_.type > Messages::DECK_CONTROL_DISABLE_LOOP) {
            this->params_.type = Messages::DECK_CONTROL_PLAY;
        }
        if (!std::isfinite(this->params_.value)) {
            this->params_.value = 1.0;
        }
        if (this->params_.quantization > Messages::DECK_QUANTIZE_BAR) {
            this->params_.quantization = Messages::DECK_QUANTIZE_IMMEDIATE;
        }
    }

    uint64_t sequence_number_{0};
};

} // namespace CommRaT