#pragma once

#include <commrat/meta/descriptor.hpp>
#include <rfl/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace musicrat::launcher {

inline constexpr const char* PORT_DIRECTION_OUTPUT = "output";
inline constexpr const char* PORT_DIRECTION_INPUT = "input";
inline constexpr const char* PORT_DIRECTION_SYNCED_INPUT = "synced_input";
inline constexpr const char* PORT_DIRECTION_REMOTE = "remote";

inline constexpr const char* PORT_DOMAIN_AUDIO = "audio";
inline constexpr const char* PORT_DOMAIN_NOTE = "note";
inline constexpr const char* PORT_DOMAIN_CONTROL = "control";
inline constexpr const char* PORT_DOMAIN_PARAMETER = "parameter";
inline constexpr const char* PORT_DOMAIN_TRANSPORT = "transport";
inline constexpr const char* PORT_DOMAIN_TELEMETRY = "telemetry";
inline constexpr const char* PORT_DOMAIN_COMMAND = "command";

inline constexpr const char* PARAMETER_KIND_CONTINUOUS = "continuous";
inline constexpr const char* PARAMETER_KIND_INTEGER = "integer";
inline constexpr const char* PARAMETER_KIND_BOOLEAN = "boolean";
inline constexpr const char* PARAMETER_KIND_CHOICE = "choice";
inline constexpr const char* PARAMETER_KIND_TEXT = "text";

inline constexpr const char* PARAMETER_SCALE_LINEAR = "linear";
inline constexpr const char* PARAMETER_SCALE_LOGARITHMIC = "logarithmic";
inline constexpr const char* PARAMETER_SCALE_DECIBEL = "decibel";

struct PhysicalPortDescriptor {
    std::string id;
    std::string display_name;
    std::string direction;
    std::size_t port_index{0};
    std::string domain;
    bool required{true};
    uint32_t max_connections{1};
};

struct PortModuleMetadata {
    uint32_t schema_version{1};
    std::vector<PhysicalPortDescriptor> ports;
};

struct ParameterChoice {
    double value{0.0};
    std::string label;
};

struct ParameterDescriptor {
    uint32_t id{0};
    std::string name;
    std::string display_name;
    std::string group;
    std::string kind;
    std::string unit;
    double minimum{0.0};
    double maximum{1.0};
    double step{0.0};
    std::string display_scale{PARAMETER_SCALE_LINEAR};
    bool automatable{false};
    bool read_only{false};
    std::vector<ParameterChoice> choices;
};

struct ParameterModuleMetadata {
    uint32_t schema_version{1};
    std::vector<ParameterDescriptor> parameters;
};

namespace detail {

inline std::optional<std::string> payload_domain(const std::string& payload) {
    if (payload == "CommRaT::Messages::AudioBlock") return PORT_DOMAIN_AUDIO;
    if (payload == "CommRaT::Messages::NoteEventBlock") return PORT_DOMAIN_NOTE;
    if (payload == "CommRaT::Messages::ParameterEventBlock") {
        return PORT_DOMAIN_PARAMETER;
    }
    if (payload == "CommRaT::Messages::DeckControlEventBlock") {
        return PORT_DOMAIN_CONTROL;
    }
    if (payload == "CommRaT::Messages::TransportBlock") {
        return PORT_DOMAIN_TRANSPORT;
    }
    if (payload == "CommRaT::Messages::LevelMeterBlock"
        || payload == "CommRaT::Messages::PlaybackStatusBlock") {
        return PORT_DOMAIN_TELEMETRY;
    }
    return std::nullopt;
}

inline const std::optional<std::vector<std::string>>* ports_for_direction(
    const commrat::ModuleDescriptor& descriptor,
    const std::string& direction) noexcept {
    if (direction == PORT_DIRECTION_OUTPUT) return &descriptor.outputs;
    if (direction == PORT_DIRECTION_INPUT) return &descriptor.inputs;
    if (direction == PORT_DIRECTION_SYNCED_INPUT) return &descriptor.synced_inputs;
    if (direction == PORT_DIRECTION_REMOTE) return &descriptor.remotes;
    return nullptr;
}

inline bool valid_port_domain(const std::string& domain) noexcept {
    return domain == PORT_DOMAIN_AUDIO
        || domain == PORT_DOMAIN_NOTE
        || domain == PORT_DOMAIN_CONTROL
        || domain == PORT_DOMAIN_PARAMETER
        || domain == PORT_DOMAIN_TRANSPORT
        || domain == PORT_DOMAIN_TELEMETRY
        || domain == PORT_DOMAIN_COMMAND;
}

inline bool valid_parameter_kind(const std::string& kind) noexcept {
    return kind == PARAMETER_KIND_CONTINUOUS
        || kind == PARAMETER_KIND_INTEGER
        || kind == PARAMETER_KIND_BOOLEAN
    || kind == PARAMETER_KIND_CHOICE
    || kind == PARAMETER_KIND_TEXT;
}

inline bool valid_parameter_scale(const std::string& scale) noexcept {
    return scale == PARAMETER_SCALE_LINEAR
        || scale == PARAMETER_SCALE_LOGARITHMIC
        || scale == PARAMETER_SCALE_DECIBEL;
}

inline std::string port_key(const PhysicalPortDescriptor& port) {
    return port.direction + ":" + std::to_string(port.port_index);
}

} // namespace detail

inline void validate_module_metadata(const commrat::ModuleDescriptor& descriptor) {
    if (!descriptor.descriptor_metadata) return;
    const auto object = descriptor.descriptor_metadata->to_object();
    if (!object) return;

    if (const auto value = object->get("musicrat_ports")) {
        const auto parsed = rfl::json::read<
            PortModuleMetadata, rfl::DefaultIfMissing>(
            rfl::json::write(value.value()));
        if (!parsed || parsed->schema_version != 1) {
            throw std::runtime_error(
                "[MusicRaT] Invalid port metadata for module_class '"
                + descriptor.module_class + "'");
        }

        std::unordered_set<std::string> ids;
        std::unordered_set<std::string> positions;
        for (const auto& port : parsed->ports) {
            const auto invalid = [&descriptor, &port](const std::string& reason) {
                throw std::runtime_error(
                    "[MusicRaT] Invalid port metadata for module_class '"
                    + descriptor.module_class + "', port '" + port.id
                    + "': " + reason);
            };
            if (port.id.empty() || !ids.insert(port.id).second) {
                invalid("port IDs must be unique and nonempty");
            }
            if (port.display_name.empty() || !detail::valid_port_domain(port.domain)) {
                invalid("display name and known domain are required");
            }
            if (port.max_connections != 1) {
                invalid("CommRaT physical ports currently require one connection slot");
            }
            const auto* ports = detail::ports_for_direction(descriptor, port.direction);
            if (ports == nullptr || !*ports || port.port_index >= (*ports)->size()) {
                invalid("direction or port index does not exist in CommRaT descriptor");
            }
            if (!positions.insert(detail::port_key(port)).second) {
                invalid("a physical port position may only be described once");
            }
            if (port.direction == PORT_DIRECTION_REMOTE
                && port.domain != PORT_DOMAIN_COMMAND) {
                invalid("remote ports must use the command domain");
            }
            if (const auto expected = detail::payload_domain((*ports)->at(port.port_index));
                expected && port.domain != *expected) {
                invalid("domain does not match the CommRaT payload type");
            }
        }
    }

    if (const auto value = object->get("musicrat_parameters")) {
        const auto parsed = rfl::json::read<
            ParameterModuleMetadata, rfl::DefaultIfMissing>(
            rfl::json::write(value.value()));
        if (!parsed || parsed->schema_version != 1) {
            throw std::runtime_error(
                "[MusicRaT] Invalid parameter metadata for module_class '"
                + descriptor.module_class + "'");
        }

        const auto defaults = descriptor.params_defaults
            ? descriptor.params_defaults->to_object()
            : rfl::Result<rfl::Generic::Object>(
                rfl::Error("params defaults are missing"));
        std::unordered_set<uint32_t> ids;
        std::unordered_set<std::string> names;
        for (const auto& parameter : parsed->parameters) {
            const auto invalid = [&descriptor, &parameter](const std::string& reason) {
                throw std::runtime_error(
                    "[MusicRaT] Invalid parameter metadata for module_class '"
                    + descriptor.module_class + "', parameter '"
                    + parameter.name + "': " + reason);
            };
            if (parameter.id == 0 || !ids.insert(parameter.id).second) {
                invalid("parameter IDs must be unique and nonzero");
            }
            if (parameter.name.empty() || !names.insert(parameter.name).second) {
                invalid("parameter names must be unique and nonempty");
            }
            if (!defaults || !defaults->get(parameter.name)) {
                invalid("name does not exist in params defaults");
            }
            if (parameter.display_name.empty() || parameter.group.empty()) {
                invalid("display name and group are required");
            }
            if (!detail::valid_parameter_kind(parameter.kind)
                || !detail::valid_parameter_scale(parameter.display_scale)) {
                invalid("parameter kind or display scale is unknown");
            }
            if (parameter.read_only && parameter.automatable) {
                invalid("read-only parameters cannot be automatable");
            }

            const bool numeric = parameter.kind == PARAMETER_KIND_CONTINUOUS
                || parameter.kind == PARAMETER_KIND_INTEGER;
            if (numeric
                && (!std::isfinite(parameter.minimum)
                    || !std::isfinite(parameter.maximum)
                    || !std::isfinite(parameter.step)
                    || parameter.minimum >= parameter.maximum
                    || parameter.step < 0.0)) {
                invalid("numeric range or step is invalid");
            }
            if (numeric && parameter.display_scale == PARAMETER_SCALE_LOGARITHMIC
                && parameter.minimum <= 0.0) {
                invalid("logarithmic ranges must be positive");
            }

            if (parameter.kind == PARAMETER_KIND_CHOICE) {
                if (parameter.choices.empty()) {
                    invalid("choice parameters require labels");
                }
                std::unordered_set<double> values;
                for (const auto& choice : parameter.choices) {
                    if (!std::isfinite(choice.value) || choice.label.empty()
                        || !values.insert(choice.value).second) {
                        invalid("choice values and labels must be unique and valid");
                    }
                }
            } else if (!parameter.choices.empty()) {
                invalid("only choice parameters may declare choices");
            }
        }
    }
}

} // namespace musicrat::launcher