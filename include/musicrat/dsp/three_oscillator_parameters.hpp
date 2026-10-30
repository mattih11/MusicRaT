#pragma once

#include <musicrat/protocol/three_oscillator.hpp>

#include <tuple>

namespace musicrat::dsp {

template<class Callback>
void visit_three_oscillator_parameters(Callback&& callback) noexcept {
    using P = CommRaT::Parameters::ThreeOscillator;
    constexpr auto members = std::tuple{
        &P::master_gain, &P::envelope_ms,
        &P::oscillator_1_waveform, &P::oscillator_1_semitones, &P::oscillator_1_level,
        &P::oscillator_2_waveform, &P::oscillator_2_semitones, &P::oscillator_2_level,
        &P::oscillator_3_waveform, &P::oscillator_3_semitones, &P::oscillator_3_level,
        &P::amplitude_attack_ms, &P::amplitude_decay_ms,
        &P::amplitude_sustain_level, &P::amplitude_release_ms,
        &P::filter_mode, &P::filter_cutoff_hz, &P::filter_resonance,
        &P::filter_attack_ms, &P::filter_decay_ms,
        &P::filter_sustain_level, &P::filter_release_ms, &P::filter_env_amount_octaves,
    };
    static_assert(std::tuple_size_v<decltype(members)> ==
        CommRaT::Parameters::THREE_OSCILLATOR_FILTER_ENV_AMOUNT_PARAMETER_ID - 1);
    uint32_t id = CommRaT::Parameters::THREE_OSCILLATOR_MASTER_GAIN_PARAMETER_ID;
    std::apply([&](auto... member) { (callback(id++, member), ...); }, members);
}

} // namespace musicrat::dsp
