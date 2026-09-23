#pragma once

#include <musicrat/protocol/control_mapping.hpp>
#include <musicrat/protocol/parameter_events.hpp>
#include <musicrat/protocol/parameter_state.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace musicrat::dsp {

using CommRaT::Parameters::CompiledControlBinding;
using CommRaT::Parameters::ControlMappingCurve;
using CommRaT::Parameters::ControlMappingMode;
using CommRaT::Parameters::CONTROL_CURVE_EXPONENTIAL;
using CommRaT::Parameters::CONTROL_CURVE_LINEAR;
using CommRaT::Parameters::CONTROL_CURVE_LOGARITHMIC;
using CommRaT::Parameters::CONTROL_MAP_ABSOLUTE;
using CommRaT::Parameters::CONTROL_MAP_CHOICE;
using CommRaT::Parameters::CONTROL_MAP_GATE;
using CommRaT::Parameters::CONTROL_MAP_MOMENTARY;
using CommRaT::Parameters::CONTROL_MAP_RELATIVE;
using CommRaT::Parameters::CONTROL_MAP_TOGGLE;
using CommRaT::Parameters::CONTROL_MAP_TRIGGER;
using CommRaT::Parameters::CONTROL_PICKUP_IMMEDIATE;
using CommRaT::Parameters::CONTROL_PICKUP_MATCH;

enum class ControlMapperConfigError : uint8_t {
    None,
    TooManyBindings,
    InvalidBinding,
    DuplicateBindingId,
};

struct ControlMapperResult {
    uint32_t mapped_count{0};
    uint32_t dropped_count{0};
    uint32_t invalid_count{0};
};

class ControlMapper {
public:
    static constexpr std::size_t MAX_BINDINGS =
        CommRaT::Parameters::ControlMapper::MAX_BINDINGS;

    template<std::size_t Size>
    [[nodiscard]] ControlMapperConfigError configure(
        const std::array<CompiledControlBinding, Size>& bindings) noexcept {
        return configure(bindings.data(), bindings.size());
    }

    [[nodiscard]] ControlMapperConfigError configure(
        const CompiledControlBinding* bindings,
        std::size_t binding_count) noexcept {
        if (binding_count > MAX_BINDINGS) {
            return ControlMapperConfigError::TooManyBindings;
        }
        if (bindings == nullptr && binding_count != 0) {
            return ControlMapperConfigError::InvalidBinding;
        }
        for (std::size_t index = 0; index < binding_count; ++index) {
            if (!valid_binding(bindings[index])) {
                return ControlMapperConfigError::InvalidBinding;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (bindings[previous].binding_id == bindings[index].binding_id) {
                    return ControlMapperConfigError::DuplicateBindingId;
                }
            }
        }

        binding_count_ = binding_count;
        for (std::size_t index = 0; index < binding_count_; ++index) {
            bindings_[index] = bindings[index];
            const double initial = std::clamp(
                bindings[index].initial_value,
                bindings[index].target_minimum,
                bindings[index].target_maximum);
            states_[index] = {
                .current_value = initial,
                .last_emitted_value = initial,
                .pickup_latched = bindings[index].pickup
                    == CONTROL_PICKUP_IMMEDIATE,
            };
        }
        return ControlMapperConfigError::None;
    }

    [[nodiscard]] ControlMapperResult process(
        const CommRaT::Messages::ControlEventBlock& input,
        uint32_t frame_count,
        CommRaT::Messages::ParameterEventBlock& output) noexcept {
        output.events.clear();
        output.timestamp_ns = input.timestamp_ns;
        output.sequence_number = input.sequence_number;
        output.flags = (input.flags & CommRaT::Messages::CONTROL_EVENT_BLOCK_OVERFLOW)
            ? static_cast<uint16_t>(
                CommRaT::Messages::PARAMETER_EVENT_BLOCK_SOURCE_OVERFLOW)
            : uint16_t{0};

        ControlMapperResult result{};
        for (const auto& event : input.events) {
            if (!valid_event(event, frame_count)) {
                ++result.invalid_count;
                continue;
            }
            for (std::size_t index = 0; index < binding_count_; ++index) {
                const auto& binding = bindings_[index];
                if (binding.source_device_id != event.source_device_id
                    || binding.source_endpoint_id != event.source_endpoint_id
                    || binding.source_kind != event.kind) {
                    continue;
                }

                auto next_state = states_[index];
                double value = 0.0;
                if (!map_value(binding, next_state, event.value, value)) {
                    states_[index] = next_state;
                    continue;
                }
                if (output.events.size()
                    >= CommRaT::Messages::ParameterEventBlock::MAX_EVENTS) {
                    output.flags |= CommRaT::Messages::PARAMETER_EVENT_BLOCK_OVERFLOW;
                    ++result.dropped_count;
                    continue;
                }
                states_[index] = next_state;
                insert_event({
                    .source_endpoint_id = event.source_endpoint_id,
                    .parameter_id = binding.target_parameter_id,
                    .sample_offset = event.sample_offset,
                    .value = value,
                    .origin_id = event.origin_id,
                    .binding_id = binding.binding_id,
                }, output);
                ++result.mapped_count;
            }
        }
        return result;
    }

    [[nodiscard]] uint32_t synchronize(
        const CommRaT::Messages::ParameterStateBlock& states) noexcept {
        uint32_t updated = 0;
        for (const auto& parameter_state : states.states) {
            if (parameter_state.parameter_id == 0
                || !std::isfinite(parameter_state.value)) {
                continue;
            }
            for (std::size_t index = 0; index < binding_count_; ++index) {
                const auto& binding = bindings_[index];
                if (binding.target_parameter_id != parameter_state.parameter_id
                    || (parameter_state.binding_id
                            != CommRaT::Messages::INVALID_CONTROL_BINDING_ID
                        && binding.binding_id != parameter_state.binding_id)) {
                    continue;
                }
                const double value = std::clamp(
                    parameter_state.value,
                    binding.target_minimum,
                    binding.target_maximum);
                states_[index].current_value = value;
                states_[index].last_emitted_value = value;
                states_[index].has_emitted = true;
                states_[index].pickup_latched = binding.pickup
                    == CONTROL_PICKUP_IMMEDIATE;
                states_[index].has_pickup_candidate = false;
                ++updated;
            }
        }
        return updated;
    }

private:
    struct BindingState {
        double current_value{0.0};
        double last_emitted_value{0.0};
        bool has_emitted{false};
        bool input_active{false};
        bool pickup_latched{true};
        bool has_pickup_candidate{false};
        double last_pickup_candidate{0.0};
    };

    static bool valid_binding(const CompiledControlBinding& binding) noexcept {
        return binding.binding_id != CommRaT::Messages::INVALID_CONTROL_BINDING_ID
            && binding.source_device_id
                != CommRaT::Messages::INVALID_CONTROL_DEVICE_ID
            && binding.source_endpoint_id
                != CommRaT::Messages::INVALID_CONTROL_ENDPOINT_ID
            && binding.target_parameter_id != 0
            && binding.source_kind <= CommRaT::Messages::CONTROL_TRIGGER
            && binding.mode <= CONTROL_MAP_CHOICE
            && binding.curve <= CONTROL_CURVE_EXPONENTIAL
            && binding.pickup <= CONTROL_PICKUP_MATCH
            && std::isfinite(binding.source_minimum)
            && std::isfinite(binding.source_maximum)
            && binding.source_minimum < binding.source_maximum
            && std::isfinite(binding.target_minimum)
            && std::isfinite(binding.target_maximum)
            && binding.target_minimum <= binding.target_maximum
            && std::isfinite(binding.scale)
            && std::isfinite(binding.offset)
            && std::isfinite(binding.dead_zone)
            && binding.dead_zone >= 0.0 && binding.dead_zone < 1.0
            && std::isfinite(binding.quantization) && binding.quantization >= 0.0
            && std::isfinite(binding.hysteresis) && binding.hysteresis >= 0.0
            && std::isfinite(binding.pickup_tolerance)
            && binding.pickup_tolerance >= 0.0
            && std::isfinite(binding.initial_value);
    }

    static bool valid_event(
        const CommRaT::Messages::ControlEvent& event,
        uint32_t frame_count) noexcept {
        return event.source_device_id
                != CommRaT::Messages::INVALID_CONTROL_DEVICE_ID
            && event.source_endpoint_id
                != CommRaT::Messages::INVALID_CONTROL_ENDPOINT_ID
            && event.kind <= CommRaT::Messages::CONTROL_TRIGGER
            && event.sample_offset < frame_count
            && std::isfinite(event.value);
    }

    static double apply_curve(double value, ControlMappingCurve curve) noexcept {
        if (curve == CONTROL_CURVE_LOGARITHMIC) return std::sqrt(value);
        if (curve == CONTROL_CURVE_EXPONENTIAL) return value * value;
        return value;
    }

    static double apply_dead_zone(double value, double dead_zone) noexcept {
        if (dead_zone == 0.0) return value;
        if (value <= dead_zone) return 0.0;
        return (value - dead_zone) / (1.0 - dead_zone);
    }

    static double finalize(
        const CompiledControlBinding& binding,
        double value) noexcept {
        if (binding.quantization > 0.0) {
            value = std::round(value / binding.quantization)
                * binding.quantization;
        }
        return std::clamp(value, binding.target_minimum, binding.target_maximum);
    }

    static bool emit_if_changed(
        const CompiledControlBinding& binding,
        BindingState& state,
        double value,
        double& output) noexcept {
        state.current_value = finalize(binding, value);
        if (state.has_emitted
            && std::abs(state.current_value - state.last_emitted_value)
                < binding.hysteresis) {
            return false;
        }
        state.last_emitted_value = state.current_value;
        state.has_emitted = true;
        output = state.current_value;
        return true;
    }

    static void insert_event(
        CommRaT::Messages::ParameterEvent event,
        CommRaT::Messages::ParameterEventBlock& output) noexcept {
        output.events.push_back(event);
        std::size_t index = output.events.size() - 1;
        while (index > 0
            && output.events[index].sample_offset
                < output.events[index - 1].sample_offset) {
            std::swap(output.events[index], output.events[index - 1]);
            --index;
        }
    }

    static bool pickup_allows(
        const CompiledControlBinding& binding,
        BindingState& state,
        double candidate) noexcept {
        if (state.pickup_latched) return true;
        const bool matched = std::abs(candidate - state.current_value)
            <= binding.pickup_tolerance;
        const bool crossed = state.has_pickup_candidate
            && (state.last_pickup_candidate - state.current_value)
                * (candidate - state.current_value) <= 0.0;
        state.last_pickup_candidate = candidate;
        state.has_pickup_candidate = true;
        state.pickup_latched = matched || crossed;
        return state.pickup_latched;
    }

    static bool map_value(
        const CompiledControlBinding& binding,
        BindingState& state,
        double input,
        double& output) noexcept {
        const bool active = input > (binding.source_minimum
            + binding.source_maximum) * 0.5;
        if (binding.mode == CONTROL_MAP_TOGGLE) {
            const bool rising = active && !state.input_active;
            state.input_active = active;
            if (!rising) return false;
            const double midpoint = (binding.target_minimum
                + binding.target_maximum) * 0.5;
            return emit_if_changed(
                binding,
                state,
                state.current_value > midpoint
                    ? binding.target_minimum
                    : binding.target_maximum,
                output);
        }
        if (binding.mode == CONTROL_MAP_TRIGGER) {
            return emit_if_changed(binding, state, binding.target_maximum, output);
        }
        if (binding.mode == CONTROL_MAP_MOMENTARY
            || binding.mode == CONTROL_MAP_GATE) {
            return emit_if_changed(
                binding,
                state,
                active ? binding.target_maximum : binding.target_minimum,
                output);
        }
        if (binding.mode == CONTROL_MAP_RELATIVE) {
            const double magnitude = apply_dead_zone(
                std::min(std::abs(input), 1.0), binding.dead_zone);
            const double curved = std::copysign(
                apply_curve(magnitude, binding.curve), input);
            return emit_if_changed(
                binding,
                state,
                state.current_value + curved * binding.scale + binding.offset,
                output);
        }

        double normalized = std::clamp(
            (input - binding.source_minimum)
                / (binding.source_maximum - binding.source_minimum),
            0.0,
            1.0);
        if (binding.invert) normalized = 1.0 - normalized;
        normalized = apply_dead_zone(normalized, binding.dead_zone);
        normalized = std::clamp(
            apply_curve(normalized, binding.curve) * binding.scale
                + binding.offset,
            0.0,
            1.0);
        const double candidate = finalize(
            binding,
            binding.target_minimum
                + normalized * (binding.target_maximum - binding.target_minimum));
        if (!pickup_allows(binding, state, candidate)) return false;
        return emit_if_changed(binding, state, candidate, output);
    }

    std::array<CompiledControlBinding, MAX_BINDINGS> bindings_{};
    std::array<BindingState, MAX_BINDINGS> states_{};
    std::size_t binding_count_{0};
};

} // namespace musicrat::dsp