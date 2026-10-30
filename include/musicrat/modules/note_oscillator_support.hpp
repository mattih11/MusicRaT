#pragma once

#include <musicrat/launcher/module_metadata.hpp>
#include <musicrat/config.hpp>
#include <musicrat/protocol/three_oscillator.hpp>
#include <musicrat/protocol/audio_block.hpp>

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace CommRaT::ModuleDetail {

inline musicrat::launcher::ParameterDescriptor note_oscillator_parameter(
    uint32_t id,
    const char* name,
    const char* display_name,
    const char* group,
    const char* unit,
    double minimum,
    double maximum,
    double step) {
    return {
        .id = id,
        .name = name,
        .display_name = display_name,
        .group = group,
        .kind = musicrat::launcher::PARAMETER_KIND_CONTINUOUS,
        .unit = unit,
        .minimum = minimum,
        .maximum = maximum,
        .step = step,
        .choices = {},
    };
}

inline musicrat::launcher::ParameterDescriptor note_oscillator_filter_mode(
    uint32_t id) {
    return {
        .id = id,
        .name = "filter_mode",
        .display_name = "Filter Mode",
        .group = "Filter",
        .kind = musicrat::launcher::PARAMETER_KIND_CHOICE,
        .unit = "",
        .choices = {
            {.value = Parameters::SYNTH_FILTER_BYPASS, .label = "Bypass"},
            {.value = Parameters::SYNTH_FILTER_LOW_PASS, .label = "Low Pass"},
            {.value = Parameters::SYNTH_FILTER_HIGH_PASS, .label = "High Pass"},
            {.value = Parameters::SYNTH_FILTER_BAND_PASS, .label = "Band Pass"},
        },
    };
}

inline std::size_t calculate_note_oscillator_frame_count(
    const commrat::ModuleConfig& config,
    double sample_rate_hz) {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz < 8000.0
        || sample_rate_hz > 192000.0) {
        throw std::invalid_argument(
            "Note oscillator sample rate must be between 8000 and 192000 Hz");
    }
    if (!config.period.has_value() || config.period->count() <= 0) {
        throw std::invalid_argument("Note oscillator requires a positive period");
    }
    const double frame_value = sample_rate_hz
        * static_cast<double>(config.period->count()) / 1000.0;
    const auto frames = static_cast<std::size_t>(std::llround(frame_value));
    if (frames == 0 || frames > Messages::AudioBlock::MAX_FRAMES) {
        throw std::invalid_argument(
            "Note oscillator period exceeds audio block capacity");
    }
    return frames;
}

} // namespace CommRaT::ModuleDetail
