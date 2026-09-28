#include <musicrat/protocol/ui_observation.hpp>

#include <sertial/message.hpp>

#include <cassert>

int main() {
    using namespace CommRaT::Messages;

    WidgetUpdateBlock original{};
    original.timestamp_ns = 1234;
    original.sequence_number = 9;
    original.events.push_back({
        .binding_id = 7,
        .surface_id = "main",
        .widget_id = "peak-meter",
        .property = WIDGET_PROPERTY_VALUE,
        .kind = WIDGET_VALUE_CONTINUOUS,
        .value = 0.75,
    });

    const auto serialized = sertial::Message<WidgetUpdateBlock>::serialize(original);
    const auto restored = sertial::Message<WidgetUpdateBlock>::deserialize(serialized.view());

    assert(restored);
    assert(restored->timestamp_ns == original.timestamp_ns);
    assert(restored->sequence_number == original.sequence_number);
    assert(restored->events.size() == 1);
    assert(restored->events[0].binding_id == 7);
    assert(restored->events[0].surface_id == "main");
    assert(restored->events[0].widget_id == "peak-meter");
    assert(restored->events[0].value == 0.75);
}
