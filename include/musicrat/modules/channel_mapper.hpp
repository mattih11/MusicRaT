#pragma once

#include <musicrat/dsp/channel_mapper_processor.hpp>
#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>

#include <stdexcept>

namespace CommRaT {

class ChannelMapper : public MusicRaT::Module2<
    commrat::Output<Messages::AudioBlock>,
    commrat::Input<Messages::AudioBlock>,
    commrat::Params<Parameters::ChannelMapper>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::AudioBlock>,
        commrat::Input<Messages::AudioBlock>,
        commrat::Params<Parameters::ChannelMapper>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        auto metadata = audio_configurable_channel_transform_metadata(
            "input_channel_count", "output_channel_count");
        metadata.musicrat_ports.ports = {
            {
                .id = "audio_out",
                .display_name = "Mapped Out",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_AUDIO,
            },
            {
                .id = "audio_in",
                .display_name = "Audio In",
                .direction = PORT_DIRECTION_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_AUDIO,
            },
        };
        metadata.musicrat_parameters.parameters = {
            {
                .id = Parameters::CHANNEL_MAPPER_MODE_PARAMETER_ID,
                .name = "mode",
                .display_name = "Mode",
                .group = "Routing",
                .kind = PARAMETER_KIND_CHOICE,
                .unit = "",
                .choices = {
                    {.value = Parameters::CHANNEL_MAP_IDENTITY, .label = "Identity"},
                    {.value = Parameters::CHANNEL_MAP_MONO_TO_STEREO, .label = "Mono to Stereo"},
                    {.value = Parameters::CHANNEL_MAP_STEREO_TO_MONO, .label = "Stereo to Mono"},
                    {.value = Parameters::CHANNEL_MAP_SWAP_STEREO, .label = "Swap Stereo"},
                    {.value = Parameters::CHANNEL_MAP_COPY_LEFT, .label = "Copy Left"},
                    {.value = Parameters::CHANNEL_MAP_COPY_RIGHT, .label = "Copy Right"},
                    {.value = Parameters::CHANNEL_MAP_MATRIX, .label = "Custom Matrix"},
                },
            },
            {
                .id = Parameters::CHANNEL_MAPPER_INPUT_CHANNELS_PARAMETER_ID,
                .name = "input_channel_count",
                .display_name = "Input Channels",
                .group = "Routing",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "channels",
                .minimum = 1.0,
                .maximum = static_cast<double>(Messages::AudioBlock::MAX_CHANNELS),
                .step = 1.0,
                .choices = {},
            },
            {
                .id = Parameters::CHANNEL_MAPPER_OUTPUT_CHANNELS_PARAMETER_ID,
                .name = "output_channel_count",
                .display_name = "Output Channels",
                .group = "Routing",
                .kind = PARAMETER_KIND_INTEGER,
                .unit = "channels",
                .minimum = 1.0,
                .maximum = static_cast<double>(Messages::AudioBlock::MAX_CHANNELS),
                .step = 1.0,
                .choices = {},
            },
        };
        return metadata;
    }

    explicit ChannelMapper(const commrat::ModuleConfig& config)
        : Base(config) {
        if (processor_.configure(this->params_)
            != musicrat::dsp::ChannelMapperConfigError::None) {
            throw std::invalid_argument("Invalid ChannelMapper configuration");
        }
    }

protected:
    void process(
        const Messages::AudioBlock& input,
        Messages::AudioBlock& output) override {
        processor_.process(input, output);
    }

    void on_params_changed() override {
        (void)processor_.configure(this->params_);
    }

private:
    musicrat::dsp::ChannelMapperProcessor processor_{};
};

} // namespace CommRaT