#include <musicrat/modules/gain.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <memory>

namespace {

class TestGain : public CommRaT::Gain {
public:
    using CommRaT::Gain::Gain;
    using CommRaT::Gain::process;
};

CommRaT::Messages::AudioBlock make_input() {
    CommRaT::Messages::AudioBlock input{};
    input.sample_rate_hz = 48000.0;
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.frame_count = 2;
    input.channel_count = 1;
    input.channels[0].push_back(1.0);
    input.channels[0].push_back(1.0);
    return input;
}

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "GainTest";
    config.outputs = commrat::MultiOutputConfig{.addresses = {
        {.system_id = 10, .instance_id = 1},
        {.system_id = 20, .instance_id = 1},
    }};
    config.inputs = commrat::MultiInputConfig{
        .sources = {{
            .system_id = 10,
            .instance_id = 2,
            .lifecycle_address = 0,
            .is_primary = true,
        }},
    };
    config.params = rfl::json::read<rfl::Generic>(R"({
        "gain": 0.5,
        "smoothing_samples": 0,
        "muted": false,
        "invert_polarity": false
    })").value();

    auto gain = std::make_unique<TestGain>(config);
    const auto input = std::make_unique<CommRaT::Messages::AudioBlock>(make_input());
    commrat::Synced<CommRaT::Messages::ParameterEventBlock> events{};
    auto output = std::make_unique<CommRaT::Messages::AudioBlock>();
    CommRaT::Messages::ParameterStateBlock state{};

    gain->process(*input, events, *output, state);

    assert(output->channels[0][0] == 0.5);
    assert(state.timestamp_ns == input->timestamp_ns);
    assert(state.sequence_number == 0);
    assert(state.flags == CommRaT::Messages::PARAMETER_STATE_BLOCK_SNAPSHOT);
    assert(state.states.size() == 1);
    assert(state.states[0].parameter_id == CommRaT::Parameters::GAIN_PARAMETER_ID);
    assert(state.states[0].value == 0.5);

    gain->process(*input, events, *output, state);
    assert(state.sequence_number == 1);
}
