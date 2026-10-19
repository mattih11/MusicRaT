#include <musicrat/dsp/parameter_state_ui_adapter.hpp>

#include <array>
#include <cassert>
#include <limits>

int main() {
    using namespace CommRaT::Messages;
    using namespace CommRaT::Parameters;

    musicrat::dsp::ParameterStateUiAdapter adapter{};
    const std::array bindings{
        CompiledParameterStateUiBinding{
            .binding_id = 1,
            .parameter_id = 1,
            .surface_id = "main",
            .widget_id = "gain",
            .parameter_minimum = 0.0,
            .parameter_maximum = 2.0,
        },
        CompiledParameterStateUiBinding{
            .binding_id = 2,
            .parameter_id = 2,
            .surface_id = "main",
            .widget_id = "pan",
            .parameter_minimum = -1.0,
            .parameter_maximum = 1.0,
        },
    };
    assert(adapter.configure(bindings)
        == musicrat::dsp::ParameterStateUiAdapterConfigError::None);

    ParameterStateBlock input{};
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.states.push_back({
        .parameter_id = 1,
        .value = 1.0,
    });
    input.states.push_back({
        .parameter_id = 2,
        .value = 0.5,
    });
    WidgetUpdateBlock output{};
    const auto result = adapter.process(input, output);
    assert(result.mapped_count == 2);
    assert(result.dropped_count == 0);
    assert(output.timestamp_ns == input.timestamp_ns);
    assert(output.sequence_number == input.sequence_number);
    assert(output.flags == 0);
    assert(output.events.size() == 2);
    assert(output.events[0].widget_id == "gain");
    assert(output.events[0].value == 0.5);
    assert(output.events[1].widget_id == "pan");
    assert(output.events[1].value == 0.75);

    input.states[0].value = 4.0;
    input.states[1].value = -2.0;
    (void)adapter.process(input, output);
    assert(output.events[0].value == 1.0);
    assert(output.events[1].value == 0.0);

    input.states[0].value = std::numeric_limits<double>::quiet_NaN();
    (void)adapter.process(input, output);
    assert(output.events.size() == 1);
    assert(output.events[0].widget_id == "pan");

    auto duplicate = bindings;
    duplicate[1].binding_id = 1;
    assert(adapter.configure(duplicate)
        == musicrat::dsp::ParameterStateUiAdapterConfigError::DuplicateBindingId);

    auto invalid = bindings;
    invalid[0].parameter_maximum = invalid[0].parameter_minimum;
    assert(adapter.configure(invalid)
        == musicrat::dsp::ParameterStateUiAdapterConfigError::InvalidBinding);
}
