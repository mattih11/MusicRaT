#pragma once

#include <musicrat/protocol/telemetry.hpp>
#include <musicrat/protocol/ui_observation.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace musicrat::dsp {

enum class LevelMeterUiAdapterConfigError : uint8_t {
    None,
    TooManyBindings,
    InvalidBinding,
    DuplicateBindingId,
};

struct LevelMeterUiAdapterResult {
    uint32_t mapped_count{0};
    uint32_t dropped_count{0};
    uint32_t invalid_count{0};
};

class LevelMeterUiAdapter {
public:
    static constexpr std::size_t MAX_BINDINGS =
        CommRaT::Parameters::LevelMeterUiAdapter::MAX_BINDINGS;

    template<std::size_t Size>
    [[nodiscard]] LevelMeterUiAdapterConfigError configure(
        const std::array<
            CommRaT::Parameters::CompiledLevelMeterObservationBinding,
            Size>& bindings) noexcept {
        return configure(bindings.data(), bindings.size());
    }

    [[nodiscard]] LevelMeterUiAdapterConfigError configure(
        const CommRaT::Parameters::CompiledLevelMeterObservationBinding* bindings,
        std::size_t binding_count) noexcept {
        if (binding_count > MAX_BINDINGS) {
            return LevelMeterUiAdapterConfigError::TooManyBindings;
        }
        if (bindings == nullptr && binding_count != 0) {
            return LevelMeterUiAdapterConfigError::InvalidBinding;
        }
        for (std::size_t index = 0; index < binding_count; ++index) {
            if (!valid_binding(bindings[index])) {
                return LevelMeterUiAdapterConfigError::InvalidBinding;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (bindings[previous].binding_id == bindings[index].binding_id) {
                    return LevelMeterUiAdapterConfigError::DuplicateBindingId;
                }
            }
        }
        binding_count_ = binding_count;
        for (std::size_t index = 0; index < binding_count_; ++index) {
            bindings_[index] = bindings[index];
        }
        return LevelMeterUiAdapterConfigError::None;
    }

    [[nodiscard]] LevelMeterUiAdapterResult process(
        const CommRaT::Messages::LevelMeterBlock& input,
        CommRaT::Messages::WidgetUpdateBlock& output) const noexcept {
        output.events.clear();
        output.timestamp_ns = input.timestamp_ns;
        output.sequence_number = input.sequence_number;
        output.flags = (input.flags & CommRaT::Messages::LEVEL_METER_INVALID) != 0
            ? static_cast<uint16_t>(
                CommRaT::Messages::WIDGET_UPDATE_BLOCK_SOURCE_INVALID)
            : uint16_t{0};

        LevelMeterUiAdapterResult result{};
        if (output.flags != 0) {
            result.invalid_count = static_cast<uint32_t>(binding_count_);
            return result;
        }
        for (std::size_t index = 0; index < binding_count_; ++index) {
            const auto& binding = bindings_[index];
            if (binding.channel >= input.channel_count) {
                ++result.invalid_count;
                continue;
            }
            if (output.events.size()
                >= CommRaT::Messages::WidgetUpdateBlock::MAX_EVENTS) {
                output.flags |= CommRaT::Messages::WIDGET_UPDATE_BLOCK_OVERFLOW;
                ++result.dropped_count;
                continue;
            }

            CommRaT::Messages::WidgetUpdate update{
                .binding_id = binding.binding_id,
                .surface_id = binding.surface_id,
                .widget_id = binding.widget_id,
                .property = binding.property,
            };
            if (binding.selector
                == CommRaT::Parameters::LEVEL_METER_OBSERVE_CLIPPED) {
                update.kind = CommRaT::Messages::WIDGET_VALUE_BOOLEAN;
                update.active = input.clipped[binding.channel] != 0;
                update.value = update.active ? 1.0 : 0.0;
            } else {
                update.kind = CommRaT::Messages::WIDGET_VALUE_CONTINUOUS;
                update.value = binding.selector
                        == CommRaT::Parameters::LEVEL_METER_OBSERVE_PEAK
                    ? input.peak[binding.channel]
                    : input.rms[binding.channel];
                update.active = update.value != 0.0;
            }
            output.events.push_back(update);
            ++result.mapped_count;
        }
        return result;
    }

private:
    static bool valid_binding(
        const CommRaT::Parameters::CompiledLevelMeterObservationBinding& binding) noexcept {
        return binding.binding_id != CommRaT::Messages::INVALID_CONTROL_BINDING_ID
            && !binding.surface_id.empty()
            && !binding.widget_id.empty()
            && binding.property <= CommRaT::Messages::WIDGET_PROPERTY_ACTIVE
            && binding.selector <= CommRaT::Parameters::LEVEL_METER_OBSERVE_CLIPPED
            && binding.channel < CommRaT::Messages::LevelMeterBlock::MAX_CHANNELS
            && (binding.selector != CommRaT::Parameters::LEVEL_METER_OBSERVE_CLIPPED
                || binding.property == CommRaT::Messages::WIDGET_PROPERTY_ACTIVE);
    }

    std::array<
        CommRaT::Parameters::CompiledLevelMeterObservationBinding,
        MAX_BINDINGS> bindings_{};
    std::size_t binding_count_{0};
};

} // namespace musicrat::dsp
