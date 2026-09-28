#include <musicrat/modules/channel_mapper.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <memory>

namespace {

class TestChannelMapper : public CommRaT::ChannelMapper {
public:
    using CommRaT::ChannelMapper::ChannelMapper;
    using CommRaT::ChannelMapper::process;
};

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "ChannelMapperTest";
    config.outputs = commrat::MultiOutputConfig{.addresses = {{
        .system_id = 10,
        .instance_id = 2,
    }}};
    config.inputs = commrat::SingleInputConfig{
        .source_system_id = 10,
        .source_instance_id = 1,
        .source_lifecycle_address = 0,
    };
    config.params = rfl::json::read<rfl::Generic>(R"({
        "mode": 1,
        "input_channel_count": 1,
        "output_channel_count": 2,
        "coefficients": []
    })").value();

    auto mapper = std::make_unique<TestChannelMapper>(config);
    auto input = std::make_unique<CommRaT::Messages::AudioBlock>();
    input->sample_rate_hz = 48000.0;
    input->frame_count = 2;
    input->channel_count = 1;
    input->channels[0].push_back(0.25F);
    input->channels[0].push_back(-0.5F);
    auto output = std::make_unique<CommRaT::Messages::AudioBlock>();
    mapper->process(*input, *output);

    assert(output->channel_count == 2);
    assert(output->channels[0][0] == 0.25F);
    assert(output->channels[1][0] == 0.25F);

    const auto metadata = CommRaT::ChannelMapper::descriptor_metadata();
    assert(metadata.musicrat_audio.inputs[0].channel_count_param
        == "input_channel_count");
    assert(metadata.musicrat_audio.outputs[0].channel_count_param
        == "output_channel_count");
    assert(!metadata.musicrat_audio.passthroughs[0].preserve_channel_count);
}