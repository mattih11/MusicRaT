#pragma once

#include <musicrat/dsp/note_voice_bank.hpp>
#include <musicrat/dsp/note_synth_parameters.hpp>
#include <musicrat/dsp/note_voice_filter.hpp>
#include <musicrat/dsp/waveforms.hpp>
#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/note_events.hpp>
#include <musicrat/protocol/wavetable_oscillator.hpp>
#include <musicrat/utility/audio_block.hpp>
#include <musicrat/utility/note_events.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace musicrat::dsp {

class WavetableOscillatorProcessor {
public:
    using Sample = CommRaT::Messages::AudioBlock::Sample;
    static constexpr std::size_t table_size = config::max_audio_frames;

    explicit WavetableOscillatorProcessor(
        const CommRaT::Parameters::WavetableOscillator& parameters = {}) noexcept {
        set_parameters(parameters);
    }

    void set_parameters(
        CommRaT::Parameters::WavetableOscillator parameters) noexcept {
        if (!std::isfinite(parameters.sample_rate_hz)) {
            parameters.sample_rate_hz = config::default_sample_rate_hz;
        }
        parameters.sample_rate_hz = std::clamp(
            parameters.sample_rate_hz, 8000.0, 192000.0);
        if (parameters.waveform
            > CommRaT::Parameters::SYNTH_WAVEFORM_REVERSE_SAWTOOTH) {
            parameters.waveform = CommRaT::Parameters::SYNTH_WAVEFORM_SINE;
        }
        if (!std::isfinite(parameters.gain)) {
            parameters.gain = Sample{0.2};
        }
        parameters.gain = std::clamp(parameters.gain, Sample{0}, Sample{1});
        normalize_note_synth_parameters(parameters);
        parameters_ = parameters;
        build_table();
    }

    void process(
        const CommRaT::Messages::NoteEventBlock* events,
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

        std::size_t event_index = 0;
        for (uint32_t frame = 0; frame < frame_count; ++frame) {
            while (events != nullptr && event_index < events->events.size()
                && events->events[event_index].sample_offset == frame) {
                const auto voice_index = voices_.apply(
                    events->events[event_index],
                    amplitude_adsr(parameters_),
                    filter_adsr(parameters_),
                    parameters_.sample_rate_hz);
                if (voice_index < NoteVoiceBank::voice_count) {
                    phases_[voice_index] = 0.0;
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
                const double frequency = 440.0 * std::exp2(
                    (static_cast<double>(voice.key) - 69.0) / 12.0);
                if (frequency >= parameters_.sample_rate_hz * 0.49) {
                    continue;
                }
                const Sample filtered = filters_[voice_index].process(
                    static_cast<Sample>(read_table(phases_[voice_index])),
                    parameters_.filter_mode,
                    parameters_.filter_cutoff_hz,
                    parameters_.filter_resonance,
                    filter_envelope,
                    parameters_.filter_env_amount_octaves,
                    parameters_.sample_rate_hz);
                mixed_sample += static_cast<double>(filtered)
                    * envelope * parameters_.gain;
                phases_[voice_index] += frequency / parameters_.sample_rate_hz;
                phases_[voice_index] -= std::floor(phases_[voice_index]);
            }
            output.channels[0].push_back(static_cast<Sample>(mixed_sample));
        }
    }

    void reset() noexcept {
        voices_.clear();
        phases_.fill(0.0);
        for (auto& filter : filters_) {
            filter.reset();
        }
    }

private:
    void build_table() noexcept {
        for (std::size_t index = 0; index < table_.size(); ++index) {
            const double phase = static_cast<double>(index)
                / static_cast<double>(table_.size());
            double sample = 0.0;
            switch (parameters_.waveform) {
            case CommRaT::Parameters::SYNTH_WAVEFORM_SQUARE:
                sample = CommRaT::Waveforms::Square{}(phase);
                break;
            case CommRaT::Parameters::SYNTH_WAVEFORM_TRIANGLE:
                sample = CommRaT::Waveforms::Triangle{}(phase);
                break;
            case CommRaT::Parameters::SYNTH_WAVEFORM_SAWTOOTH:
                sample = CommRaT::Waveforms::Sawtooth{}(phase);
                break;
            case CommRaT::Parameters::SYNTH_WAVEFORM_REVERSE_SAWTOOTH:
                sample = CommRaT::Waveforms::ReverseSawtooth{}(phase);
                break;
            case CommRaT::Parameters::SYNTH_WAVEFORM_SINE:
            default:
                sample = CommRaT::Waveforms::Sin{}(phase);
                break;
            }
            table_[index] = static_cast<Sample>(sample);
        }
    }

    [[nodiscard]] double read_table(double phase) const noexcept {
        const double position = phase * static_cast<double>(table_.size());
        const auto index = static_cast<std::size_t>(position) % table_.size();
        const auto next_index = (index + 1) % table_.size();
        const double fraction = position - static_cast<double>(index);
        const double first = static_cast<double>(table_[index]);
        const double second = static_cast<double>(table_[next_index]);
        return first + (second - first) * fraction;
    }

    CommRaT::Parameters::WavetableOscillator parameters_{};
    NoteVoiceBank voices_{};
    std::array<Sample, table_size> table_{};
    std::array<double, NoteVoiceBank::voice_count> phases_{};
    std::array<NoteVoiceFilter, NoteVoiceBank::voice_count> filters_{};
};

} // namespace musicrat::dsp
