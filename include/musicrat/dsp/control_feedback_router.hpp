#pragma once

#include <musicrat/protocol/control_feedback.hpp>
#include <musicrat/protocol/parameter_state.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace musicrat::dsp {

enum class ControlFeedbackRouterConfigError : uint8_t {
    None,
    TooManyBindings,
    InvalidBinding,
    DuplicateBinding,
};

struct ControlFeedbackRouterResult {
    uint32_t routed_count{0};
    uint32_t suppressed_count{0};
    uint32_t dropped_count{0};
};

class ControlFeedbackRouter {
public:
    static constexpr std::size_t MAX_BINDINGS =
        CommRaT::Messages::ControlFeedbackBlock::MAX_EVENTS;

    [[nodiscard]] ControlFeedbackRouterConfigError configure(
        const CommRaT::Parameters::CompiledControlFeedbackBinding* bindings,
        std::size_t binding_count) noexcept {
        binding_count_ = 0;
        if (binding_count > MAX_BINDINGS) {
            return ControlFeedbackRouterConfigError::TooManyBindings;
        }
        for (std::size_t index = 0; index < binding_count; ++index) {
            const auto& binding = bindings[index];
            if (binding.binding_id == CommRaT::Messages::INVALID_CONTROL_BINDING_ID
                || binding.target_parameter_id == 0
                || binding.destination_device_id
                    == CommRaT::Messages::INVALID_CONTROL_DEVICE_ID
                || binding.destination_endpoint_id
                    == CommRaT::Messages::INVALID_CONTROL_ENDPOINT_ID
                || binding.suppress_origin_id
                    == CommRaT::Messages::INVALID_CONTROL_ORIGIN_ID) {
                return ControlFeedbackRouterConfigError::InvalidBinding;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (bindings[previous].binding_id == binding.binding_id) {
                    return ControlFeedbackRouterConfigError::DuplicateBinding;
                }
            }
            bindings_[index] = binding;
        }
        binding_count_ = binding_count;
        return ControlFeedbackRouterConfigError::None;
    }

    [[nodiscard]] ControlFeedbackRouterResult process(
        const CommRaT::Messages::ParameterStateBlock& input,
        CommRaT::Messages::ControlFeedbackBlock& output) const noexcept {
        ControlFeedbackRouterResult result{};
        output.events.clear();
        output.timestamp_ns = input.timestamp_ns;
        output.sequence_number = input.sequence_number;
        output.flags = (input.flags & CommRaT::Messages::PARAMETER_STATE_BLOCK_OVERFLOW)
            ? CommRaT::Messages::CONTROL_FEEDBACK_BLOCK_SOURCE_OVERFLOW
            : 0;

        for (const auto& state : input.states) {
            if (state.parameter_id == 0 || !std::isfinite(state.value)) {
                continue;
            }
            for (std::size_t index = 0; index < binding_count_; ++index) {
                const auto& binding = bindings_[index];
                if (binding.target_parameter_id != state.parameter_id
                    || (state.binding_id
                            != CommRaT::Messages::INVALID_CONTROL_BINDING_ID
                        && binding.binding_id != state.binding_id)) {
                    continue;
                }
                if (state.origin_id == binding.suppress_origin_id) {
                    ++result.suppressed_count;
                    continue;
                }
                if (output.events.size()
                    >= CommRaT::Messages::ControlFeedbackBlock::MAX_EVENTS) {
                    output.flags |= CommRaT::Messages::CONTROL_FEEDBACK_BLOCK_OVERFLOW;
                    ++result.dropped_count;
                    continue;
                }
                output.events.push_back({
                    .destination_device_id = binding.destination_device_id,
                    .destination_endpoint_id = binding.destination_endpoint_id,
                    .origin_id = state.origin_id,
                    .binding_id = binding.binding_id,
                    .value = state.value,
                });
                ++result.routed_count;
            }
        }
        return result;
    }

private:
    std::array<CommRaT::Parameters::CompiledControlFeedbackBinding, MAX_BINDINGS>
        bindings_{};
    std::size_t binding_count_{0};
};

} // namespace musicrat::dsp
