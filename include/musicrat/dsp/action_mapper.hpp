#pragma once

#include <musicrat/protocol/action_mapping.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace musicrat::dsp {

enum class ActionMapperConfigError : uint8_t {
    None,
    TooManyBindings,
    InvalidBinding,
    DuplicateBindingId,
};

struct ActionMapperResult {
    uint32_t mapped_count{0};
    uint32_t dropped_count{0};
    uint32_t invalid_count{0};
};

class ActionMapper {
public:
    static constexpr std::size_t MAX_BINDINGS =
        CommRaT::Parameters::ActionMapper::MAX_BINDINGS;

    template<std::size_t Size>
    [[nodiscard]] ActionMapperConfigError configure(
        const std::array<CommRaT::Parameters::CompiledActionBinding, Size>& bindings) noexcept {
        return configure(bindings.data(), bindings.size());
    }

    [[nodiscard]] ActionMapperConfigError configure(
        const CommRaT::Parameters::CompiledActionBinding* bindings,
        std::size_t binding_count) noexcept {
        if (binding_count > MAX_BINDINGS) return ActionMapperConfigError::TooManyBindings;
        if (bindings == nullptr && binding_count != 0) {
            return ActionMapperConfigError::InvalidBinding;
        }
        for (std::size_t index = 0; index < binding_count; ++index) {
            if (!valid_binding(bindings[index])) return ActionMapperConfigError::InvalidBinding;
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (bindings[previous].binding_id == bindings[index].binding_id) {
                    return ActionMapperConfigError::DuplicateBindingId;
                }
            }
        }
        binding_count_ = binding_count;
        for (std::size_t index = 0; index < binding_count_; ++index) {
            bindings_[index] = bindings[index];
        }
        return ActionMapperConfigError::None;
    }

    [[nodiscard]] ActionMapperResult process(
        const CommRaT::Messages::ControlEventBlock& input,
        uint32_t frame_count,
        CommRaT::Messages::DeckControlEventBlock& output) noexcept {
        output.events.clear();
        output.timestamp_ns = input.timestamp_ns;
        output.sequence_number = input.sequence_number;
        ActionMapperResult result{};
        for (const auto& event : input.events) {
            if (event.sample_offset >= frame_count || !std::isfinite(event.value)) {
                ++result.invalid_count;
                continue;
            }
            for (std::size_t index = 0; index < binding_count_; ++index) {
                const auto& binding = bindings_[index];
                if (event.source_device_id != binding.source_device_id
                    || event.source_endpoint_id != binding.source_endpoint_id
                    || event.kind != binding.source_kind) {
                    continue;
                }
                if ((event.kind == CommRaT::Messages::CONTROL_TRIGGER
                        || event.kind == CommRaT::Messages::CONTROL_BOOLEAN
                        || event.kind == CommRaT::Messages::CONTROL_GATE)
                    && event.value <= 0.0) {
                    continue;
                }
                if (output.events.size() >= CommRaT::Messages::DeckControlEventBlock::MAX_EVENTS) {
                    ++result.dropped_count;
                    continue;
                }
                double value = binding.default_value;
                if (binding.source_kind == CommRaT::Messages::CONTROL_UNIPOLAR
                    || binding.source_kind == CommRaT::Messages::CONTROL_BIPOLAR
                    || binding.source_kind == CommRaT::Messages::CONTROL_RELATIVE) {
                    const double normalized = std::clamp(
                        (event.value - binding.source_minimum)
                            / (binding.source_maximum - binding.source_minimum),
                        0.0, 1.0);
                    value = binding.target_minimum
                        + normalized * (binding.target_maximum - binding.target_minimum);
                }
                output.events.push_back({
                    .type = binding.action_type,
                    .sample_offset = event.sample_offset,
                    .value = value,
                    .ramp_frames = binding.ramp_frames,
                    .quantization = binding.quantization,
                });
                ++result.mapped_count;
            }
        }
        return result;
    }

private:
    static bool valid_binding(
        const CommRaT::Parameters::CompiledActionBinding& binding) noexcept {
        return binding.binding_id != CommRaT::Messages::INVALID_CONTROL_BINDING_ID
            && binding.source_device_id != CommRaT::Messages::INVALID_CONTROL_DEVICE_ID
            && binding.source_endpoint_id != CommRaT::Messages::INVALID_CONTROL_ENDPOINT_ID
            && binding.source_kind <= CommRaT::Messages::CONTROL_TRIGGER
            && binding.action_type <= CommRaT::Messages::DECK_CONTROL_DISABLE_LOOP
            && binding.quantization <= CommRaT::Messages::DECK_QUANTIZE_BAR
            && std::isfinite(binding.source_minimum)
            && std::isfinite(binding.source_maximum)
            && binding.source_minimum < binding.source_maximum
            && std::isfinite(binding.target_minimum)
            && std::isfinite(binding.target_maximum)
            && binding.target_minimum <= binding.target_maximum
            && std::isfinite(binding.default_value);
    }

    std::array<CommRaT::Parameters::CompiledActionBinding, MAX_BINDINGS> bindings_{};
    std::size_t binding_count_{0};
};

} // namespace musicrat::dsp
