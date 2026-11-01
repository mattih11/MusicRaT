#pragma once

#include <musicrat/protocol/ui_observation.hpp>

namespace CommRaT::Parameters {

using PlaybackObservationSelector = uint8_t;

inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_RATE = 0;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_MEDIA_POSITION = 1;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_DURATION = 2;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_BEAT_POSITION = 3;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_READY = 4;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_PLAYING = 5;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_LOOPING = 6;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_WORKER_STATE = 7;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_DECODE_ERROR = 8;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_POSITION = 9;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_ELAPSED_TIME = 10;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_REMAINING_TIME = 11;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_LOAD_STATE = 12;
inline constexpr PlaybackObservationSelector PLAYBACK_OBSERVE_LOAD_DECODE_ERROR = 13;

struct CompiledPlaybackObservationBinding {
    CommRaT::Messages::ControlBindingId binding_id{
        CommRaT::Messages::INVALID_CONTROL_BINDING_ID};
    sertial::fixed_string<64> surface_id{};
    sertial::fixed_string<64> widget_id{};
    CommRaT::Messages::WidgetProperty property{
        CommRaT::Messages::WIDGET_PROPERTY_VALUE};
    PlaybackObservationSelector selector{PLAYBACK_OBSERVE_POSITION};
    double minimum{0.0};
    double maximum{1.0};
};

struct PlaybackStatusUiAdapter {
    static constexpr std::size_t MAX_BINDINGS = musicrat::config::max_parameter_events;

    sertial::fixed_vector<CompiledPlaybackObservationBinding, MAX_BINDINGS> bindings{};
};

} // namespace CommRaT::Parameters
