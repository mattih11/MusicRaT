#pragma once

#include <musicrat/dsp/adsr_envelope.hpp>
#include <musicrat/config.hpp>
#include <musicrat/protocol/note_events.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace musicrat::dsp {

struct NoteVoice {
    uint32_t source_endpoint_id{0};
    uint32_t note_id{0};
    uint8_t channel{0};
    uint8_t key{0};
    bool active{false};
    bool releasing{false};
    double velocity{0.0};
    double envelope{0.0};
    double envelope_step{0.0};
    AdsrEnvelope amplitude_envelope{};
    AdsrEnvelope filter_envelope{};

    [[nodiscard]] bool matches(
        const CommRaT::Messages::NoteEvent& event) const noexcept {
        return active
            && source_endpoint_id == event.source_endpoint_id
            && note_id == event.note_id
            && channel == event.channel
            && key == event.key;
    }

    void start(
        const CommRaT::Messages::NoteEvent& event,
        const AdsrParameters& amplitude_parameters,
        const AdsrParameters& filter_parameters,
        double sample_rate_hz) noexcept {
        source_endpoint_id = event.source_endpoint_id;
        note_id = event.note_id;
        channel = event.channel;
        key = event.key;
        active = true;
        releasing = false;
        velocity = std::clamp(static_cast<double>(event.value), 0.0, 1.0);
        envelope = 0.0;
        envelope_step = 0.0;
        amplitude_envelope.start(amplitude_parameters, sample_rate_hz, velocity);
        filter_envelope.start(filter_parameters, sample_rate_hz);
    }

    void start(
        const CommRaT::Messages::NoteEvent& event,
        uint32_t envelope_frames) noexcept {
        const double milliseconds =
            static_cast<double>(envelope_frames) * 1000.0 / 48000.0;
        const AdsrParameters parameters{
            .attack_ms = milliseconds,
            .decay_ms = 0.0,
            .sustain_level = 1.0,
            .release_ms = milliseconds,
        };
        start(event, parameters, parameters, 48000.0);
    }

    void release() noexcept {
        if (!active || releasing) {
            return;
        }
        releasing = true;
        amplitude_envelope.release();
        filter_envelope.release();
    }

    [[nodiscard]] double next_envelope() noexcept {
        if (!active) {
            return 0.0;
        }
        envelope = amplitude_envelope.next();
        if (!amplitude_envelope.active()) {
            active = false;
        }
        return envelope;
    }

    [[nodiscard]] double next_filter_envelope() noexcept {
        return filter_envelope.next();
    }
};

class NoteVoiceBank {
public:
    static constexpr std::size_t voice_count = config::max_synth_voices;

    [[nodiscard]] static uint32_t envelope_frames(
        double envelope_ms,
        double sample_rate_hz) noexcept {
        const double bounded_ms = std::clamp(envelope_ms, 1.0, 100.0);
        const double frames = bounded_ms * sample_rate_hz / 1000.0;
        return std::max<uint32_t>(1, static_cast<uint32_t>(frames));
    }

    [[nodiscard]] std::size_t apply(
        const CommRaT::Messages::NoteEvent& event,
        const AdsrParameters& amplitude_parameters,
        const AdsrParameters& filter_parameters,
        double sample_rate_hz) noexcept {
        if (event.type == CommRaT::Messages::NOTE_ON && event.value > 0.0F) {
            std::size_t selected = voices_.size();
            for (std::size_t index = 0; index < voices_.size(); ++index) {
                if (voices_[index].matches(event)) {
                    selected = index;
                    break;
                }
            }
            if (selected == voices_.size()) {
                for (std::size_t index = 0; index < voices_.size(); ++index) {
                    if (!voices_[index].active) {
                        selected = index;
                        break;
                    }
                }
            }
            if (selected == voices_.size()) {
                selected = next_voice_to_steal_;
                next_voice_to_steal_ = (next_voice_to_steal_ + 1) % voices_.size();
            }
            voices_[selected].start(
                event, amplitude_parameters, filter_parameters, sample_rate_hz);
            return selected;
        }

        if (event.type == CommRaT::Messages::NOTE_ALL_OFF) {
            for (auto& voice : voices_) {
                if (voice.active
                    && voice.source_endpoint_id == event.source_endpoint_id
                    && voice.channel == event.channel) {
                    voice.release();
                }
            }
            return voices_.size();
        }

        if (event.type == CommRaT::Messages::NOTE_OFF
            || event.type == CommRaT::Messages::NOTE_ON) {
            for (auto& voice : voices_) {
                if (voice.matches(event)) {
                    voice.release();
                }
            }
        }
        return voices_.size();
    }

    [[nodiscard]] std::size_t apply(
        const CommRaT::Messages::NoteEvent& event,
        uint32_t fade_frames) noexcept {
        const double milliseconds =
            static_cast<double>(fade_frames) * 1000.0 / 48000.0;
        const AdsrParameters parameters{
            .attack_ms = milliseconds,
            .decay_ms = 0.0,
            .sustain_level = 1.0,
            .release_ms = milliseconds,
        };
        return apply(event, parameters, parameters, 48000.0);
    }

    [[nodiscard]] NoteVoice& operator[](std::size_t index) noexcept {
        return voices_[index];
    }

    [[nodiscard]] const NoteVoice& operator[](std::size_t index) const noexcept {
        return voices_[index];
    }

    void clear() noexcept {
        for (auto& voice : voices_) {
            voice = {};
        }
        next_voice_to_steal_ = 0;
    }

private:
    std::array<NoteVoice, voice_count> voices_{};
    std::size_t next_voice_to_steal_{0};
};

} // namespace musicrat::dsp
