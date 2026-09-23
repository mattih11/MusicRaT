#include <musicrat/launcher/audio_format_validation.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <stdexcept>
#include <string>

namespace {

struct SourceParams {
    double sample_rate_hz;
};

struct SinkParams {
    double sample_rate_hz;
    uint16_t channel_count;
};

struct EmptyParams {};

using DescriptorMap =
    std::unordered_map<std::string, commrat::ModuleDescriptor>;

rfl::Generic generic(const auto& value) {
    return rfl::json::read<rfl::Generic>(rfl::json::write(value)).value();
}

commrat::ModuleDescriptor descriptor(
    std::string module_class,
    std::vector<std::string> outputs,
    std::vector<std::string> inputs,
    const musicrat::launcher::DescriptorMetadata& metadata,
    rfl::Generic defaults) {
    return {
        .module_class = std::move(module_class),
        .binary = "/bin/true",
        .outputs = std::move(outputs),
        .inputs = std::move(inputs),
        .synced_inputs = std::vector<std::string>{},
        .remotes = std::vector<std::string>{},
        .execution_mode = std::nullopt,
        .default_period_ms = std::nullopt,
        .cmd_messages = std::nullopt,
        .command_endpoints = std::nullopt,
        .lifecycle_endpoint = std::nullopt,
        .params_defaults = std::move(defaults),
        .descriptor_metadata = generic(metadata),
    };
}

commrat::ModuleDescription passthrough(
    std::string name,
    std::string module_class,
    uint8_t output_instance,
    uint8_t input_instance) {
    return {
        .name = std::move(name),
        .module_class = std::move(module_class),
        .module_address = std::nullopt,
        .outputs = {{.system_id = 10, .instance_id = output_instance}},
        .inputs = {{
            .source_system_id = 10,
            .source_instance_id = input_instance,
        }},
        .synced_inputs = std::nullopt,
        .remotes = std::nullopt,
        .period_ms = std::nullopt,
        .params = std::nullopt,
    };
}

commrat::AppDescription app(
    double sample_rate_hz,
    uint16_t channel_count,
    bool with_processors = false) {
    std::vector<commrat::ModuleDescription> modules{
        {
            .name = "Oscillator_1",
            .module_class = "Oscillator",
            .module_address = std::nullopt,
            .outputs = {{.system_id = 10, .instance_id = 1}},
            .inputs = {},
            .synced_inputs = std::nullopt,
            .remotes = std::nullopt,
            .period_ms = std::nullopt,
            .params = generic(SourceParams{sample_rate_hz}),
        },
    };
    uint8_t sink_input_instance = 1;
    if (with_processors) {
        modules.push_back(passthrough("Gain_1", "Gain", 2, 1));
        modules.push_back(passthrough("LevelMeter_1", "LevelMeter", 3, 2));
        sink_input_instance = 3;
    }
    modules.push_back({
        .name = "WavSink_1",
        .module_class = "WavSink",
        .module_address = commrat::ModuleAddressDescription{
            .system_id = 20, .instance_id = 1},
        .outputs = {},
        .inputs = {{
            .source_system_id = 10,
            .source_instance_id = sink_input_instance,
        }},
        .synced_inputs = std::nullopt,
        .remotes = std::nullopt,
        .period_ms = std::nullopt,
        .params = generic(SinkParams{48000.0, channel_count}),
    });
    return {
        .app_name = "AudioFormatValidation",
        .modules = std::move(modules),
        .descriptor_dirs = std::nullopt,
        .companions = std::nullopt,
    };
}

commrat::AppDescription panner_app(
    double sample_rate_hz,
    uint16_t sink_channel_count) {
    std::vector<commrat::ModuleDescription> modules{
        {
            .name = "Oscillator_1",
            .module_class = "Oscillator",
            .module_address = std::nullopt,
            .outputs = {{.system_id = 10, .instance_id = 1}},
            .inputs = {},
            .synced_inputs = std::nullopt,
            .remotes = std::nullopt,
            .period_ms = std::nullopt,
            .params = generic(SourceParams{sample_rate_hz}),
        },
        passthrough("Panner_1", "Panner", 2, 1),
        {
            .name = "WavSink_1",
            .module_class = "WavSink",
            .module_address = commrat::ModuleAddressDescription{
                .system_id = 20, .instance_id = 1},
            .outputs = {},
            .inputs = {{
                .source_system_id = 10,
                .source_instance_id = 2,
            }},
            .synced_inputs = std::nullopt,
            .remotes = std::nullopt,
            .period_ms = std::nullopt,
            .params = generic(SinkParams{48000.0, sink_channel_count}),
        },
    };
    return {
        .app_name = "PannerFormatValidation",
        .modules = std::move(modules),
        .descriptor_dirs = std::nullopt,
        .companions = std::nullopt,
    };
}

bool fails_with(const commrat::AppDescription& description,
                const DescriptorMap& descriptors,
                const std::string& expected) {
    try {
        musicrat::launcher::validate_audio_formats(description, descriptors);
    } catch (const std::runtime_error& error) {
        return std::string(error.what()).find(expected) != std::string::npos;
    }
    return false;
}

} // namespace

int main() {
    DescriptorMap descriptors;
    descriptors.emplace("Oscillator", descriptor(
        "Oscillator", {"CommRaT::Messages::AudioBlock"}, {},
        musicrat::launcher::audio_source_metadata("sample_rate_hz", 1),
        generic(SourceParams{48000.0})));
    descriptors.emplace("WavSink", descriptor(
        "WavSink", {}, {"CommRaT::Messages::AudioBlock"},
        musicrat::launcher::audio_sink_metadata("sample_rate_hz", "channel_count"),
        generic(SinkParams{48000.0, 1})));
    descriptors.emplace("Gain", descriptor(
        "Gain", {"CommRaT::Messages::AudioBlock"},
        {"CommRaT::Messages::AudioBlock"},
        musicrat::launcher::audio_passthrough_metadata(), generic(EmptyParams{})));
    descriptors.emplace("LevelMeter", descriptor(
        "LevelMeter", {"CommRaT::Messages::AudioBlock"},
        {"CommRaT::Messages::AudioBlock"},
        musicrat::launcher::audio_passthrough_metadata(), generic(EmptyParams{})));
    descriptors.emplace("Panner", descriptor(
        "Panner", {"CommRaT::Messages::AudioBlock"},
        {"CommRaT::Messages::AudioBlock"},
        musicrat::launcher::audio_channel_transform_metadata(1, 2),
        generic(EmptyParams{})));

    musicrat::launcher::validate_audio_formats(app(48000.0, 1), descriptors);
    assert(fails_with(app(44100.0, 1), descriptors, "sample-rate mismatch"));
    assert(fails_with(app(48000.0, 2), descriptors, "channel-count mismatch"));
    musicrat::launcher::validate_audio_formats(app(48000.0, 1, true), descriptors);
    assert(fails_with(
        app(44100.0, 1, true), descriptors, "sample-rate mismatch"));
    musicrat::launcher::validate_audio_formats(
        panner_app(48000.0, 2), descriptors);
    assert(fails_with(
        panner_app(48000.0, 1), descriptors, "channel-count mismatch"));
    assert(fails_with(
        panner_app(44100.0, 2), descriptors, "sample-rate mismatch"));
    return 0;
}