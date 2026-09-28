#pragma once

#include <musicrat/protocol/ui_observation.hpp>

#include <cstdint>

namespace musicrat::dsp {

struct WidgetUpdateMergeResult {
    uint32_t merged_count{0};
    uint32_t dropped_count{0};
};

class WidgetUpdateMerger {
public:
    [[nodiscard]] WidgetUpdateMergeResult process(
        const CommRaT::Messages::WidgetUpdateBlock& primary,
        const CommRaT::Messages::WidgetUpdateBlock* secondary,
        CommRaT::Messages::WidgetUpdateBlock& output) const noexcept {
        using namespace CommRaT::Messages;

        output.events.clear();
        output.timestamp_ns = primary.timestamp_ns;
        output.sequence_number = primary.sequence_number;
        output.flags = 0;

        WidgetUpdateMergeResult result{};
        bool has_valid_source = false;
        append(primary, output, result, has_valid_source);
        if (secondary != nullptr) append(*secondary, output, result, has_valid_source);
        if (!has_valid_source) output.flags |= WIDGET_UPDATE_BLOCK_SOURCE_INVALID;
        return result;
    }

private:
    static void append(
        const CommRaT::Messages::WidgetUpdateBlock& input,
        CommRaT::Messages::WidgetUpdateBlock& output,
        WidgetUpdateMergeResult& result,
        bool& has_valid_source) noexcept {
        using namespace CommRaT::Messages;

        if ((input.flags & WIDGET_UPDATE_BLOCK_SOURCE_INVALID) != 0) return;
        has_valid_source = true;
        if ((input.flags & WIDGET_UPDATE_BLOCK_OVERFLOW) != 0) {
            output.flags |= WIDGET_UPDATE_BLOCK_OVERFLOW;
        }
        for (const auto& event : input.events) {
            if (output.events.size() >= WidgetUpdateBlock::MAX_EVENTS) {
                output.flags |= WIDGET_UPDATE_BLOCK_OVERFLOW;
                ++result.dropped_count;
                continue;
            }
            output.events.push_back(event);
            ++result.merged_count;
        }
    }
};

} // namespace musicrat::dsp