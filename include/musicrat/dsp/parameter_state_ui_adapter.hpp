#pragma once

#include <musicrat/protocol/parameter_state.hpp>
#include <musicrat/protocol/ui_observation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace musicrat::dsp {

enum class ParameterStateUiAdapterConfigError : uint8_t {
    None,
    TooManyBindings,
    InvalidBinding,
    DuplicateBindingId,
};

struct ParameterStateUiAdapterResult {
    uint32_t mapped_count{0};
    uint32_t dropped_count{0};
};

class ParameterStateUiAdapter {
public:
    static constexpr std::size_t MAX_BINDINGS =
        CommRaT::Parameters::ParameterStateUiAdapter::MAX_BINDINGS;

    template<std::size_t Size>
    [[nodiscard]] ParameterStateUiAdapterConfigError configure(
        const std::array<
            CommRaT::Parameters::CompiledParameterStateUiBinding,
            Size>& bindings) noexcept {
        return configure(bindings.data(), bindings.size());
    }

    [[nodiscard]] ParameterStateUiAdapterConfigError configure(
        const CommRaT::Parameters::CompiledParameterStateUiBinding* bindings,
        std::size_t binding_count) noexcept {
        if (binding_count > MAX_BINDINGS) {
            return ParameterStateUiAdapterConfigError::TooManyBindings;
        }
        if (bindings == nullptr && binding_count != 0) {
            return ParameterStateUiAdapterConfigError::InvalidBinding;
        }
        for (std::size_t index = 0; index < binding_count; ++index) {
            if (!valid_binding(bindings[index])) {
                return ParameterStateUiAdapterConfigError::InvalidBinding;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (bindings[previous].binding_id == bindings[index].binding_id) {
                    return ParameterStateUiAdapterConfigError::DuplicateBindingId;
                }
            }
        }
        binding_count_ = binding_count;
        for (std::size_t index = 0; index < binding_count_; ++index) {
            bindings_[index] = bindings[index];
        }
        return ParameterStateUiAdapterConfigError::None;
    }

    [[nodiscard]] ParameterStateUiAdapterResult process(
        const CommRaT::Messages::ParameterStateBlock& input,
        CommRaT::Messages::WidgetUpdateBlock& output) const noexcept {
        using namespace CommRaT::Messages;

        output.events.clear();
        output.timestamp_ns = input.timestamp_ns;
        output.sequence_number = input.sequence_number;
        output.flags = (input.flags & PARAMETER_STATE_BLOCK_OVERFLOW) != 0
            ? static_cast<uint16_t>(WIDGET_UPDATE_BLOCK_OVERFLOW)
            : uint16_t{0};

        ParameterStateUiAdapterResult result{};
        for (const auto& state : input.states) {
            if (state.parameter_id == 0 || !std::isfinite(state.value)) {
                continue;
            }
            for (std::size_t index = 0; index < binding_count_; ++index) {
                const auto& binding = bindings_[index];
                if (binding.parameter_id != state.parameter_id) {
                    continue;
                }
                if (output.events.size() >= WidgetUpdateBlock::MAX_EVENTS) {
                    output.flags |= WIDGET_UPDATE_BLOCK_OVERFLOW;
                    ++result.dropped_count;
                    continue;
                }
                const double normalized = std::clamp(
                    (state.value - binding.parameter_minimum)
                        / (binding.parameter_maximum - binding.parameter_minimum),
                    0.0,
                    1.0);
                output.events.push_back({
                    .binding_id = binding.binding_id,
                    .surface_id = binding.surface_id,
                    .widget_id = binding.widget_id,
                    .property = WIDGET_PROPERTY_VALUE,
                    .kind = WIDGET_VALUE_CONTINUOUS,
                    .value = normalized,
                    .active = normalized != 0.0,
                });
                ++result.mapped_count;
            }
        }
        return result;
    }

private:
    static bool valid_binding(
        const CommRaT::Parameters::CompiledParameterStateUiBinding& binding) noexcept {
        return binding.binding_id != CommRaT::Messages::INVALID_CONTROL_BINDING_ID
            && binding.parameter_id != 0
            && !binding.surface_id.empty()
            && !binding.widget_id.empty()
            && std::isfinite(binding.parameter_minimum)
            && std::isfinite(binding.parameter_maximum)
            && binding.parameter_minimum < binding.parameter_maximum;
    }

    std::array<
        CommRaT::Parameters::CompiledParameterStateUiBinding,
        MAX_BINDINGS> bindings_{};
    std::size_t binding_count_{0};
};

} // namespace musicrat::dsp
