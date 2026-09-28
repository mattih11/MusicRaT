#include <musicrat/dsp/action_mapper.hpp>

#include <array>
#include <cassert>
#include <cmath>

int main() {
    using namespace CommRaT::Messages;
    using namespace CommRaT::Parameters;

    musicrat::dsp::ActionMapper mapper{};
    const std::array bindings{
        CompiledActionBinding{
            .binding_id = 1,
            .source_device_id = 2,
            .source_endpoint_id = 3,
            .source_kind = CONTROL_TRIGGER,
            .action_type = DECK_CONTROL_PLAY,
            .default_value = 1.0,
        },
        CompiledActionBinding{
            .binding_id = 2,
            .source_device_id = 2,
            .source_endpoint_id = 4,
            .source_kind = CONTROL_UNIPOLAR,
            .action_type = DECK_CONTROL_SET_RATE,
            .quantization = DECK_QUANTIZE_BEAT,
            .source_minimum = 0.0,
            .source_maximum = 1.0,
            .target_minimum = 0.5,
            .target_maximum = 2.0,
            .ramp_frames = 128,
        },
    };
    assert(mapper.configure(bindings) == musicrat::dsp::ActionMapperConfigError::None);

    ControlEventBlock input{};
    input.timestamp_ns = 12;
    input.sequence_number = 8;
    input.events.push_back({
        .source_device_id = 2,
        .source_endpoint_id = 3,
        .sample_offset = 3,
        .kind = CONTROL_TRIGGER,
        .value = 1.0,
    });
    input.events.push_back({
        .source_device_id = 2,
        .source_endpoint_id = 3,
        .sample_offset = 4,
        .kind = CONTROL_TRIGGER,
        .value = 0.0,
    });
    input.events.push_back({
        .source_device_id = 2,
        .source_endpoint_id = 4,
        .sample_offset = 5,
        .kind = CONTROL_UNIPOLAR,
        .value = 0.5,
    });

    DeckControlEventBlock output{};
    const auto result = mapper.process(input, 16, output);
    assert(result.mapped_count == 2);
    assert(result.invalid_count == 0);
    assert(output.timestamp_ns == 12);
    assert(output.sequence_number == 8);
    assert(output.events.size() == 2);
    assert(output.events[0].type == DECK_CONTROL_PLAY);
    assert(output.events[0].sample_offset == 3);
    assert(output.events[1].type == DECK_CONTROL_SET_RATE);
    assert(output.events[1].sample_offset == 5);
    assert(std::abs(output.events[1].value - 1.25) < 1.0e-12);
    assert(output.events[1].ramp_frames == 128);
    assert(output.events[1].quantization == DECK_QUANTIZE_BEAT);

    auto duplicate = bindings;
    duplicate[1].binding_id = 1;
    assert(mapper.configure(duplicate)
        == musicrat::dsp::ActionMapperConfigError::DuplicateBindingId);
}
