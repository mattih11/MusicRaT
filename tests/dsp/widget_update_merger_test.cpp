#include <musicrat/dsp/widget_update_merger.hpp>

#include <cassert>

int main() {
    using namespace CommRaT::Messages;

    WidgetUpdateBlock primary{};
    primary.timestamp_ns = 100;
    primary.sequence_number = 7;
    primary.events.push_back({.binding_id = 1, .widget_id = "first"});
    WidgetUpdateBlock secondary{};
    secondary.events.push_back({.binding_id = 2, .widget_id = "second"});

    musicrat::dsp::WidgetUpdateMerger merger{};
    WidgetUpdateBlock output{};
    const auto merged = merger.process(primary, &secondary, output);
    assert(merged.merged_count == 2);
    assert(merged.dropped_count == 0);
    assert(output.timestamp_ns == 100);
    assert(output.sequence_number == 7);
    assert(output.flags == 0);
    assert(output.events.size() == 2);
    assert(output.events[0].binding_id == 1);
    assert(output.events[1].binding_id == 2);

    secondary.events.clear();
    secondary.flags = WIDGET_UPDATE_BLOCK_SOURCE_INVALID;
    assert(merger.process(primary, &secondary, output).merged_count == 1);
    assert(output.flags == 0);

    primary.events.clear();
    primary.flags = WIDGET_UPDATE_BLOCK_SOURCE_INVALID;
    assert(merger.process(primary, &secondary, output).merged_count == 0);
    assert(output.flags == WIDGET_UPDATE_BLOCK_SOURCE_INVALID);

    primary.flags = 0;
    for (std::size_t index = 0; index < WidgetUpdateBlock::MAX_EVENTS; ++index) {
        primary.events.push_back({.binding_id = static_cast<ControlBindingId>(index + 1)});
    }
    secondary.flags = 0;
    secondary.events.push_back({.binding_id = 100});
    const auto overflow = merger.process(primary, &secondary, output);
    assert(overflow.merged_count == WidgetUpdateBlock::MAX_EVENTS);
    assert(overflow.dropped_count == 1);
    assert((output.flags & WIDGET_UPDATE_BLOCK_OVERFLOW) != 0);
}