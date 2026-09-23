#include <musicrat/launcher/module_metadata.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

rfl::Generic generic(const auto& value) {
    return rfl::json::read<rfl::Generic>(rfl::json::write(value)).value();
}

commrat::ModuleDescriptor note_consumer(
    musicrat::launcher::PortModuleMetadata ports) {
    struct Metadata {
        musicrat::launcher::PortModuleMetadata musicrat_ports;
    };
    return {
        .module_class = "TestInstrument",
        .binary = "/bin/true",
        .outputs = std::vector<std::string>{
            "CommRaT::Messages::AudioBlock",
        },
        .inputs = std::vector<std::string>{
            "CommRaT::Messages::NoteEventBlock",
        },
        .synced_inputs = std::vector<std::string>{
            "CommRaT::Messages::ParameterEventBlock",
        },
        .remotes = std::vector<std::string>{},
        .execution_mode = std::nullopt,
        .default_period_ms = std::nullopt,
        .cmd_messages = std::nullopt,
        .command_endpoints = std::nullopt,
        .lifecycle_endpoint = std::nullopt,
        .params_defaults = std::nullopt,
        .descriptor_metadata = generic(Metadata{std::move(ports)}),
    };
}

commrat::ModuleDescriptor parameterized_module(
    musicrat::launcher::ParameterModuleMetadata parameters) {
    struct TestParams {
        double amount{1.0};
        bool enabled{true};
        std::string path{"input.wav"};
    };
    struct Metadata {
        musicrat::launcher::ParameterModuleMetadata musicrat_parameters;
    };
    return {
        .module_class = "TestProcessor",
        .binary = "/bin/true",
        .outputs = std::vector<std::string>{},
        .inputs = std::vector<std::string>{},
        .synced_inputs = std::vector<std::string>{},
        .remotes = std::vector<std::string>{},
        .execution_mode = std::nullopt,
        .default_period_ms = std::nullopt,
        .cmd_messages = std::nullopt,
        .command_endpoints = std::nullopt,
        .lifecycle_endpoint = std::nullopt,
        .params_defaults = generic(TestParams{}),
        .descriptor_metadata = generic(Metadata{std::move(parameters)}),
    };
}

bool rejects(const std::function<void()>& function) {
    try {
        function();
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

musicrat::launcher::PortModuleMetadata valid_ports() {
    using namespace musicrat::launcher;
    return {
        .schema_version = 1,
        .ports = {
            {
                .id = "audio_out",
                .display_name = "Audio Out",
                .direction = PORT_DIRECTION_OUTPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_AUDIO,
            },
            {
                .id = "note_events",
                .display_name = "Notes",
                .direction = PORT_DIRECTION_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_NOTE,
            },
            {
                .id = "parameter_events",
                .display_name = "Parameter Events",
                .direction = PORT_DIRECTION_SYNCED_INPUT,
                .port_index = 0,
                .domain = PORT_DOMAIN_PARAMETER,
            },
        },
    };
}

musicrat::launcher::ParameterModuleMetadata valid_parameters() {
    using namespace musicrat::launcher;
    return {
        .schema_version = 1,
        .parameters = {
            {
                .id = 1,
                .name = "amount",
                .display_name = "Amount",
                .group = "Main",
                .kind = PARAMETER_KIND_CONTINUOUS,
                .minimum = 0.0,
                .maximum = 2.0,
                .step = 0.01,
                .automatable = true,
            },
            {
                .id = 2,
                .name = "enabled",
                .display_name = "Enabled",
                .group = "Main",
                .kind = PARAMETER_KIND_BOOLEAN,
            },
            {
                .id = 3,
                .name = "path",
                .display_name = "Path",
                .group = "Main",
                .kind = PARAMETER_KIND_TEXT,
            },
        },
    };
}

} // namespace

int main() {
    using namespace musicrat::launcher;

    validate_module_metadata(note_consumer(valid_ports()));

    auto wrong_note_domain = valid_ports();
    wrong_note_domain.ports[1].domain = PORT_DOMAIN_CONTROL;
    assert(rejects([&] {
        validate_module_metadata(note_consumer(wrong_note_domain));
    }));

    auto out_of_range = valid_ports();
    out_of_range.ports[1].port_index = 1;
    assert(rejects([&] {
        validate_module_metadata(note_consumer(out_of_range));
    }));

    auto duplicate_position = valid_ports();
    duplicate_position.ports.push_back({
        .id = "other_notes",
        .display_name = "Other Notes",
        .direction = PORT_DIRECTION_INPUT,
        .port_index = 0,
        .domain = PORT_DOMAIN_NOTE,
    });
    assert(rejects([&] {
        validate_module_metadata(note_consumer(duplicate_position));
    }));

    validate_module_metadata(parameterized_module(valid_parameters()));

    auto duplicate_parameter_id = valid_parameters();
    duplicate_parameter_id.parameters[1].id = 1;
    assert(rejects([&] {
        validate_module_metadata(parameterized_module(duplicate_parameter_id));
    }));

    auto missing_default = valid_parameters();
    missing_default.parameters[0].name = "unknown";
    assert(rejects([&] {
        validate_module_metadata(parameterized_module(missing_default));
    }));

    auto malformed_range = valid_parameters();
    malformed_range.parameters[0].minimum = 2.0;
    malformed_range.parameters[0].maximum = 1.0;
    assert(rejects([&] {
        validate_module_metadata(parameterized_module(malformed_range));
    }));
}