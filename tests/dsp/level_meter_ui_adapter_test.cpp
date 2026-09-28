#include <musicrat/dsp/level_meter_ui_adapter.hpp>

#include <array>
#include <cassert>

int main() {
    using namespace CommRaT::Messages;
    using namespace CommRaT::Parameters;

    musicrat::dsp::LevelMeterUiAdapter adapter{};
    const std::array bindings{
        CompiledLevelMeterObservationBinding{
            .binding_id = 1,
            .surface_id = "main",
            .widget_id = "left-peak",
            .property = WIDGET_PROPERTY_VALUE,
            .selector = LEVEL_METER_OBSERVE_PEAK,
            .channel = 0,
        },
        CompiledLevelMeterObservationBinding{
            .binding_id = 2,
            .surface_id = "main",
            .widget_id = "right-clip",
            .property = WIDGET_PROPERTY_ACTIVE,
            .selector = LEVEL_METER_OBSERVE_CLIPPED,
            .channel = 1,
        },
        CompiledLevelMeterObservationBinding{
            .binding_id = 3,
            .surface_id = "main",
            .widget_id = "missing-channel",
            .property = WIDGET_PROPERTY_VALUE,
            .selector = LEVEL_METER_OBSERVE_RMS,
            .channel = 2,
        },
    };
    assert(adapter.configure(bindings)
        == musicrat::dsp::LevelMeterUiAdapterConfigError::None);

    LevelMeterBlock input{};
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.channel_count = 2;
    input.peak[0] = 0.75;
    input.clipped[1] = 1;

    WidgetUpdateBlock output{};
    const auto result = adapter.process(input, output);
    assert(result.mapped_count == 2);
    assert(result.invalid_count == 1);
    assert(result.dropped_count == 0);
    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.flags == 0);
    assert(output.events.size() == 2);
    assert(output.events[0].binding_id == 1);
    assert(output.events[0].surface_id == "main");
    assert(output.events[0].widget_id == "left-peak");
    assert(output.events[0].kind == WIDGET_VALUE_CONTINUOUS);
    assert(output.events[0].value == 0.75);
    assert(output.events[1].binding_id == 2);
    assert(output.events[1].kind == WIDGET_VALUE_BOOLEAN);
    assert(output.events[1].active);

    input.flags = LEVEL_METER_INVALID;
    const auto invalid = adapter.process(input, output);
    assert(invalid.invalid_count == bindings.size());
    assert(output.events.empty());
    assert(output.flags == WIDGET_UPDATE_BLOCK_SOURCE_INVALID);

    auto duplicate = bindings;
    duplicate[1].binding_id = 1;
    assert(adapter.configure(duplicate)
        == musicrat::dsp::LevelMeterUiAdapterConfigError::DuplicateBindingId);

    auto wrong_property = bindings;
    wrong_property[1].property = WIDGET_PROPERTY_VALUE;
    assert(adapter.configure(wrong_property)
        == musicrat::dsp::LevelMeterUiAdapterConfigError::InvalidBinding);
}
