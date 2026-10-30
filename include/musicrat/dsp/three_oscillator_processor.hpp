#pragma once

#include <musicrat/dsp/note_voice_bank.hpp>
#include <musicrat/dsp/note_synth_parameters.hpp>
#include <musicrat/dsp/note_voice_filter.hpp>
#include <musicrat/dsp/three_oscillator_parameters.hpp>
#include <musicrat/protocol/parameter_events.hpp>
#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/note_events.hpp>
#include <musicrat/protocol/three_oscillator.hpp>
#include <musicrat/utility/audio_block.hpp>
#include <musicrat/utility/note_events.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <type_traits>

namespace musicrat::dsp {

class ThreeOscillatorProcessor {
public:
    using Sample = CommRaT::Messages::AudioBlock::Sample;

    explicit ThreeOscillatorProcessor(
        const CommRaT::Parameters::ThreeOscillator& parameters = {}) noexcept {
        set_parameters(parameters);
    }

    void set_parameters(
        CommRaT::Parameters::ThreeOscillator parameters) noexcept {
        if (!std::isfinite(parameters.sample_rate_hz)) {
            parameters.sample_rate_hz = config::default_sample_rate_hz;
        }
        parameters.sample_rate_hz = std::clamp(
            parameters.sample_rate_hz, 8000.0, 192000.0);
        parameters.master_gain = static_cast<Sample>(std::clamp(
            finite_or(parameters.master_gain, 0.2), 0.0, 1.0));
        normalize_note_synth_parameters(parameters);
        normalize_oscillator(
            parameters.oscillator_1_waveform,
            parameters.oscillator_1_semitones,
            parameters.oscillator_1_level);
        normalize_oscillator(
            parameters.oscillator_2_waveform,
            parameters.oscillator_2_semitones,
            parameters.oscillator_2_level);
        normalize_oscillator(
            parameters.oscillator_3_waveform,
            parameters.oscillator_3_semitones,
            parameters.oscillator_3_level);
        parameters_ = parameters;
    }

    void process(
        const CommRaT::Messages::NoteEventBlock* events,
        uint32_t frame_count,
        uint64_t timestamp_ns,
        uint64_t sequence_number,
        CommRaT::Messages::AudioBlock& output) noexcept {
        process(events, nullptr, frame_count, timestamp_ns, sequence_number, output);
    }

    [[nodiscard]] const CommRaT::Parameters::ThreeOscillator& parameters() const noexcept {
        return parameters_;
    }

    void process(
        const CommRaT::Messages::NoteEventBlock* events,
        const CommRaT::Messages::ParameterEventBlock* parameter_events,
        uint32_t frame_count,
        uint64_t timestamp_ns,
        uint64_t sequence_number,
        CommRaT::Messages::AudioBlock& output) noexcept {
        clear_audio_channels(output);
        output.sample_rate_hz = parameters_.sample_rate_hz;
        output.timestamp_ns = timestamp_ns;
        output.sequence_number = sequence_number;
        output.frame_count = frame_count;
        output.channel_count = 1;
        output.flags = 0;

        if (frame_count > CommRaT::Messages::AudioBlock::MAX_FRAMES) {
            mark_invalid_audio_block(output);
            reset();
            return;
        }
        if (events != nullptr
            && validate_note_event_block(*events, frame_count)
                != NoteEventValidationError::None) {
            mark_invalid_audio_block(output);
            reset();
            return;
        }
        if (parameter_events != nullptr) {
            uint32_t previous = 0;
            for (const auto& event : parameter_events->events) {
                if (event.sample_offset >= frame_count || event.sample_offset < previous
                    || !std::isfinite(event.value)
                    || event.parameter_id < CommRaT::Parameters::THREE_OSCILLATOR_MASTER_GAIN_PARAMETER_ID
                    || event.parameter_id > CommRaT::Parameters::THREE_OSCILLATOR_FILTER_ENV_AMOUNT_PARAMETER_ID) {
                    mark_invalid_audio_block(output);
                    return;
                }
                previous = event.sample_offset;
            }
        }

        std::size_t event_index = 0;
        std::size_t parameter_index = 0;
        for (uint32_t frame = 0; frame < frame_count; ++frame) {
            while (parameter_events != nullptr && parameter_index < parameter_events->events.size()
                && parameter_events->events[parameter_index].sample_offset == frame) {
                const auto& event = parameter_events->events[parameter_index++];
                auto updated = parameters_;
                visit_three_oscillator_parameters([&](uint32_t id, auto member) {
                    if (id != event.parameter_id) return;
                    using Value = std::remove_reference_t<decltype(updated.*member)>;
                    if constexpr (std::is_integral_v<Value>) {
                        const double maximum = id == CommRaT::Parameters::THREE_OSCILLATOR_FILTER_MODE_PARAMETER_ID
                            ? static_cast<double>(CommRaT::Parameters::SYNTH_FILTER_BAND_PASS)
                            : static_cast<double>(CommRaT::Parameters::SYNTH_WAVEFORM_REVERSE_SAWTOOTH);
                        updated.*member = static_cast<Value>(std::round(std::clamp(event.value, 0.0, maximum)));
                    } else {
                        updated.*member = static_cast<Value>(event.value);
                    }
                });
                set_parameters(updated);
            }
            while (events != nullptr && event_index < events->events.size()
                && events->events[event_index].sample_offset == frame) {
                const auto voice_index = voices_.apply(
                    events->events[event_index],
                    amplitude_adsr(parameters_),
                    filter_adsr(parameters_),
                    parameters_.sample_rate_hz);
                if (voice_index < NoteVoiceBank::voice_count) {
                    phases_[voice_index].fill(0.0);
                    filters_[voice_index].reset();
                }
                ++event_index;
            }

            double mixed_sample = 0.0;
            for (std::size_t voice_index = 0;
                 voice_index < NoteVoiceBank::voice_count;
                 ++voice_index) {
                auto& voice = voices_[voice_index];
                const double envelope = voice.next_envelope();
                if (envelope == 0.0 && !voice.active) {
                    continue;
                }
                const double filter_envelope = voice.next_filter_envelope();
                const double note_frequency = 440.0 * std::exp2(
                    (static_cast<double>(voice.key) - 69.0) / 12.0);
                double voice_sample = render_oscillator(
                    parameters_.oscillator_1_waveform,
                    parameters_.oscillator_1_semitones,
                    parameters_.oscillator_1_level,
                    phases_[voice_index][0], note_frequency);
                voice_sample += render_oscillator(
                    parameters_.oscillator_2_waveform,
                    parameters_.oscillator_2_semitones,
                    parameters_.oscillator_2_level,
                    phases_[voice_index][1], note_frequency);
                voice_sample += render_oscillator(
                    parameters_.oscillator_3_waveform,
                    parameters_.oscillator_3_semitones,
                    parameters_.oscillator_3_level,
                    phases_[voice_index][2], note_frequency);
                voice_sample = filters_[voice_index].process(
                    static_cast<Sample>(voice_sample),
                    parameters_.filter_mode,
                    parameters_.filter_cutoff_hz,
                    parameters_.filter_resonance,
                    filter_envelope,
                    parameters_.filter_env_amount_octaves,
                    parameters_.sample_rate_hz);
                mixed_sample += static_cast<double>(voice_sample)
                    * envelope * parameters_.master_gain;
            }
            output.channels[0].push_back(static_cast<Sample>(mixed_sample));
        }
    }

    void reset() noexcept {
        voices_.clear();
        for (auto& phase : phases_) {
            phase.fill(0.0);
        }
        for (auto& filter : filters_) {
            filter.reset();
        }
    }

private:
    static double finite_or(double value, double fallback) noexcept {
        return std::isfinite(value) ? value : fallback;
    }

    static void normalize_oscillator(
        uint8_t& waveform,
        double& semitones,
        Sample& level) noexcept {
        if (waveform > CommRaT::Parameters::SYNTH_WAVEFORM_REVERSE_SAWTOOTH) {
            waveform = CommRaT::Parameters::SYNTH_WAVEFORM_SINE;
        }
        semitones = std::clamp(finite_or(semitones, 0.0), -48.0, 48.0);
        level = static_cast<Sample>(
            std::clamp(finite_or(level, 0.0), 0.0, 1.0));
    }

    double render_oscillator(
        uint8_t waveform,
        double semitones,
        Sample level,
        double& phase,
        double note_frequency) const noexcept {
        const double frequency = note_frequency * std::exp2(semitones / 12.0);
        if (frequency >= parameters_.sample_rate_hz * 0.49) {
            return 0.0;
        }
        double sample = 0.0;
        switch (waveform) {
        case CommRaT::Parameters::SYNTH_WAVEFORM_SQUARE:
            sample = phase < 0.5 ? 1.0 : -1.0;
            break;
        case CommRaT::Parameters::SYNTH_WAVEFORM_TRIANGLE:
            sample = phase < 0.25 ? 4.0 * phase
                : (phase < 0.75 ? 2.0 - 4.0 * phase : 4.0 * phase - 4.0);
            break;
        case CommRaT::Parameters::SYNTH_WAVEFORM_SAWTOOTH:
            sample = 2.0 * phase - 1.0;
            break;
        case CommRaT::Parameters::SYNTH_WAVEFORM_REVERSE_SAWTOOTH:
            sample = 1.0 - 2.0 * phase;
            break;
        case CommRaT::Parameters::SYNTH_WAVEFORM_SINE:
        default:
            sample = std::sin(2.0 * std::numbers::pi * phase);
            break;
        }
        phase += frequency / parameters_.sample_rate_hz;
        phase -= std::floor(phase);
        return sample * static_cast<double>(level);
    }

    CommRaT::Parameters::ThreeOscillator parameters_{};
    NoteVoiceBank voices_{};
    std::array<std::array<double, 3>, NoteVoiceBank::voice_count> phases_{};
    std::array<NoteVoiceFilter, NoteVoiceBank::voice_count> filters_{};
};

} // namespace musicrat::dsp
