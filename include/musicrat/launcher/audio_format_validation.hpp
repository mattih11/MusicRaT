#pragma once

#include <musicrat/config.hpp>

#include <commrat/launcher/module_description.hpp>
#include <commrat/meta/descriptor.hpp>
#include <rfl/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace musicrat::launcher {

struct AudioEndpointFormat {
    std::size_t port_index{0};
    std::optional<double> sample_rate_hz;
    std::optional<std::string> sample_rate_param;
    std::optional<uint16_t> channel_count;
    std::optional<std::string> channel_count_param;
    uint16_t max_channels{static_cast<uint16_t>(config::max_audio_channels)};
    uint32_t max_frames{static_cast<uint32_t>(config::max_audio_frames)};
    std::optional<std::string> clock_domain;
};

struct AudioPassthrough {
    std::size_t input_port_index{0};
    std::size_t output_port_index{0};
};

struct AudioModuleFormat {
    uint32_t schema_version{1};
    std::vector<AudioEndpointFormat> outputs;
    std::vector<AudioEndpointFormat> inputs;
    std::vector<AudioPassthrough> passthroughs;
};

struct DescriptorMetadata {
    AudioModuleFormat musicrat_audio;
};

inline DescriptorMetadata audio_source_metadata(
    std::string sample_rate_param,
    std::optional<uint16_t> channel_count = std::nullopt) {
    return {
        .musicrat_audio = {
            .outputs = {{
                .port_index = 0,
                .sample_rate_hz = std::nullopt,
                .sample_rate_param = std::move(sample_rate_param),
                .channel_count = channel_count,
                .channel_count_param = std::nullopt,
                .clock_domain = std::nullopt,
            }},
            .inputs = {},
            .passthroughs = {},
        },
    };
}

inline DescriptorMetadata audio_sink_metadata(
    std::string sample_rate_param,
    std::string channel_count_param) {
    return {
        .musicrat_audio = {
            .outputs = {},
            .inputs = {{
                .port_index = 0,
                .sample_rate_hz = std::nullopt,
                .sample_rate_param = std::move(sample_rate_param),
                .channel_count = std::nullopt,
                .channel_count_param = std::move(channel_count_param),
                .clock_domain = std::nullopt,
            }},
            .passthroughs = {},
        },
    };
}

inline DescriptorMetadata audio_passthrough_metadata(
    std::size_t input_port_index = 0,
    std::size_t output_port_index = 0) {
    AudioEndpointFormat input{
        .port_index = input_port_index,
        .sample_rate_hz = std::nullopt,
        .sample_rate_param = std::nullopt,
        .channel_count = std::nullopt,
        .channel_count_param = std::nullopt,
        .clock_domain = std::nullopt,
    };
    AudioEndpointFormat output{
        .port_index = output_port_index,
        .sample_rate_hz = std::nullopt,
        .sample_rate_param = std::nullopt,
        .channel_count = std::nullopt,
        .channel_count_param = std::nullopt,
        .clock_domain = std::nullopt,
    };
    return {
        .musicrat_audio = {
            .outputs = {std::move(output)},
            .inputs = {std::move(input)},
            .passthroughs = {{
                .input_port_index = input_port_index,
                .output_port_index = output_port_index,
            }},
        },
    };
}

namespace detail {

inline std::optional<AudioModuleFormat> parse_audio_format(
    const commrat::ModuleDescriptor& descriptor) {
    if (!descriptor.descriptor_metadata) return std::nullopt;
    const auto object = descriptor.descriptor_metadata->to_object();
    if (!object) return std::nullopt;
    const auto audio = object->get("musicrat_audio");
    if (!audio) return std::nullopt;
    const auto parsed = rfl::json::read<AudioModuleFormat, rfl::DefaultIfMissing>(
        rfl::json::write(audio.value()));
    if (!parsed || parsed->schema_version != 1) {
        throw std::runtime_error(
            "[MusicRaT] Invalid audio descriptor metadata for module_class '" +
            descriptor.module_class + "'");
    }
    return parsed.value();
}

inline std::optional<double> numeric_parameter(
    const std::optional<rfl::Generic>& params,
    const std::string& name) {
    if (!params) return std::nullopt;
    const auto object = params->to_object();
    if (!object) return std::nullopt;
    const auto value = object->get(name);
    if (!value) return std::nullopt;
    const auto number = value->to_double();
    if (!number) return std::nullopt;
    return number.value();
}

inline std::optional<double> resolve(
    const std::optional<double>& fixed,
    const std::optional<std::string>& parameter,
    const commrat::ModuleDescription& module,
    const commrat::ModuleDescriptor& descriptor) {
    if (fixed) return fixed;
    if (!parameter) return std::nullopt;
    if (const auto configured = numeric_parameter(module.params, *parameter)) {
        return configured;
    }
    return numeric_parameter(descriptor.params_defaults, *parameter);
}

inline const AudioEndpointFormat* find_endpoint(
    const std::vector<AudioEndpointFormat>& endpoints,
    std::size_t port_index) {
    for (const auto& endpoint : endpoints) {
        if (endpoint.port_index == port_index) return &endpoint;
    }
    return nullptr;
}

struct ResolvedEndpoint {
    AudioEndpointFormat declaration;
    std::optional<double> sample_rate_hz;
    std::optional<double> channel_count;
    std::optional<std::string> clock_domain;
};

struct ModuleState {
    const commrat::ModuleDescription* module;
    const commrat::ModuleDescriptor* descriptor;
    AudioModuleFormat format;
    std::vector<ResolvedEndpoint> outputs;
    std::vector<ResolvedEndpoint> inputs;
};

inline ResolvedEndpoint resolve_endpoint(
    const AudioEndpointFormat& endpoint,
    const commrat::ModuleDescription& module,
    const commrat::ModuleDescriptor& descriptor) {
    ResolvedEndpoint resolved{
        .declaration = endpoint,
        .sample_rate_hz = resolve(
            endpoint.sample_rate_hz, endpoint.sample_rate_param,
            module, descriptor),
        .channel_count = resolve(
            endpoint.channel_count, endpoint.channel_count_param,
            module, descriptor),
        .clock_domain = endpoint.clock_domain,
    };
    if (resolved.sample_rate_hz && *resolved.sample_rate_hz <= 0.0) {
        throw std::runtime_error("[MusicRaT] Audio sample rates must be positive");
    }
    if (resolved.channel_count
        && (*resolved.channel_count < 1.0
            || *resolved.channel_count > endpoint.max_channels
            || std::floor(*resolved.channel_count) != *resolved.channel_count)) {
        throw std::runtime_error(
            "[MusicRaT] Audio channel count exceeds endpoint capacity");
    }
    return resolved;
}

inline ResolvedEndpoint* find_endpoint(
    std::vector<ResolvedEndpoint>& endpoints,
    std::size_t port_index) {
    for (auto& endpoint : endpoints) {
        if (endpoint.declaration.port_index == port_index) return &endpoint;
    }
    return nullptr;
}

inline bool merge_format(
    const ResolvedEndpoint& source,
    ResolvedEndpoint& destination,
    const std::string& source_name,
    const std::string& destination_name) {
    bool changed = false;
    if (source.sample_rate_hz) {
        if (destination.sample_rate_hz
            && std::fabs(*source.sample_rate_hz - *destination.sample_rate_hz) > 1e-9) {
            throw std::runtime_error(
                "[MusicRaT] Audio sample-rate mismatch: '" + source_name +
                "' provides " + std::to_string(*source.sample_rate_hz) +
                " Hz but '" + destination_name + "' requires " +
                std::to_string(*destination.sample_rate_hz) + " Hz");
        }
        if (!destination.sample_rate_hz) {
            destination.sample_rate_hz = source.sample_rate_hz;
            changed = true;
        }
    }
    if (source.channel_count) {
        if (destination.channel_count
            && *source.channel_count != *destination.channel_count) {
            throw std::runtime_error(
                "[MusicRaT] Audio channel-count mismatch: '" + source_name +
                "' provides " + std::to_string(*source.channel_count) +
                " channels but '" + destination_name + "' requires " +
                std::to_string(*destination.channel_count));
        }
        if (!destination.channel_count) {
            destination.channel_count = source.channel_count;
            changed = true;
        }
    }
    if (source.clock_domain) {
        if (destination.clock_domain
            && source.clock_domain != destination.clock_domain) {
            throw std::runtime_error(
                "[MusicRaT] Audio clock-domain mismatch between '" +
                source_name + "' and '" + destination_name + "'");
        }
        if (!destination.clock_domain) {
            destination.clock_domain = source.clock_domain;
            changed = true;
        }
    }
    return changed;
}

} // namespace detail

inline void validate_audio_formats(
    const commrat::AppDescription& app,
    const std::unordered_map<std::string, commrat::ModuleDescriptor>& descriptors) {
    std::vector<detail::ModuleState> states;
    for (const auto& module : app.modules) {
        const auto descriptor_it = descriptors.find(module.module_class);
        if (descriptor_it == descriptors.end()) continue;
        auto format = detail::parse_audio_format(descriptor_it->second);
        if (!format) continue;
        detail::ModuleState state{
            .module = &module,
            .descriptor = &descriptor_it->second,
            .format = std::move(*format),
            .outputs = {},
            .inputs = {},
        };
        for (const auto& endpoint : state.format.outputs) {
            if (!state.descriptor->outputs
                || endpoint.port_index >= state.descriptor->outputs->size()) {
                throw std::runtime_error(
                    "[MusicRaT] Audio output metadata index is out of range for '" +
                    module.name + "'");
            }
            state.outputs.push_back(
                detail::resolve_endpoint(endpoint, module, *state.descriptor));
        }
        for (const auto& endpoint : state.format.inputs) {
            if (!state.descriptor->inputs
                || endpoint.port_index >= state.descriptor->inputs->size()) {
                throw std::runtime_error(
                    "[MusicRaT] Audio input metadata index is out of range for '" +
                    module.name + "'");
            }
            state.inputs.push_back(
                detail::resolve_endpoint(endpoint, module, *state.descriptor));
        }
        states.push_back(std::move(state));
    }

    const auto find_state = [&](const commrat::ModuleDescription& module)
        -> detail::ModuleState* {
        for (auto& state : states) {
            if (state.module == &module) return &state;
        }
        return nullptr;
    };

    const std::size_t iteration_limit = states.size() + 1;
    for (std::size_t iteration = 0; iteration < iteration_limit; ++iteration) {
        bool changed = false;
        for (auto& consumer_state : states) {
            for (auto& input_format : consumer_state.inputs) {
                const auto input_index = input_format.declaration.port_index;
                if (input_index >= consumer_state.module->inputs.size()) continue;
                const auto& route = consumer_state.module->inputs[input_index];
                const auto& expected_type =
                    consumer_state.descriptor->inputs->at(input_index);

                detail::ModuleState* producer_state = nullptr;
                std::size_t output_index = 0;
                for (const auto& candidate : app.modules) {
                    const auto descriptor_it = descriptors.find(candidate.module_class);
                    if (descriptor_it == descriptors.end()
                        || !descriptor_it->second.outputs) continue;
                    for (std::size_t index = 0; index < candidate.outputs.size(); ++index) {
                        if (candidate.outputs[index].system_id == route.source_system_id
                            && candidate.outputs[index].instance_id
                                == route.source_instance_id
                            && index < descriptor_it->second.outputs->size()
                            && descriptor_it->second.outputs->at(index) == expected_type) {
                            producer_state = find_state(candidate);
                            output_index = index;
                            break;
                        }
                    }
                    if (producer_state) break;
                }
                if (!producer_state) continue;
                auto* output_format =
                    detail::find_endpoint(producer_state->outputs, output_index);
                if (!output_format) continue;
                if (output_format->declaration.max_channels
                        > input_format.declaration.max_channels
                    || output_format->declaration.max_frames
                        > input_format.declaration.max_frames) {
                    throw std::runtime_error(
                        "[MusicRaT] Audio capacity mismatch between '" +
                        producer_state->module->name + "' and '" +
                        consumer_state.module->name + "'");
                }
                changed |= detail::merge_format(
                    *output_format, input_format,
                    producer_state->module->name,
                    consumer_state.module->name);
            }

            for (const auto& passthrough : consumer_state.format.passthroughs) {
                auto* input = detail::find_endpoint(
                    consumer_state.inputs, passthrough.input_port_index);
                auto* output = detail::find_endpoint(
                    consumer_state.outputs, passthrough.output_port_index);
                if (!input || !output) {
                    throw std::runtime_error(
                        "[MusicRaT] Invalid audio pass-through metadata for '" +
                        consumer_state.module->name + "'");
                }
                changed |= detail::merge_format(
                    *input, *output,
                    consumer_state.module->name + ".input",
                    consumer_state.module->name + ".output");
            }
        }
        if (!changed) return;
    }

    throw std::runtime_error("[MusicRaT] Audio format propagation did not converge");
}

} // namespace musicrat::launcher