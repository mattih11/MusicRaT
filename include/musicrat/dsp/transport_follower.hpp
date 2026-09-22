#pragma once

#include <musicrat/dsp/beat_grid.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace musicrat::dsp {

enum class TransportSyncAction : uint8_t {
    None,
    SetRate,
    Seek,
};

struct TransportSyncDecision {
    double target_rate{1.0};
    double phase_error_beats{0.0};
    uint64_t seek_frame{0};
    TransportSyncAction action{TransportSyncAction::None};
    bool playing{false};
    bool valid{false};
};

class TransportFollower {
public:
    struct Settings {
        double minimum_rate{0.25};
        double maximum_rate{4.0};
        double phase_gain{0.1};
        double maximum_nudge{0.08};
        double seek_threshold_beats{0.5};
    };

    explicit TransportFollower(
        const CommRaT::Messages::BeatGrid& grid) noexcept
        : TransportFollower(grid, Settings{}) {}

    TransportFollower(
        const CommRaT::Messages::BeatGrid& grid,
        Settings settings) noexcept
        : mapper_(grid)
        , settings_(settings)
        , valid_(mapper_.valid() && valid_settings(settings)) {}

    [[nodiscard]] bool valid() const noexcept {
        return valid_;
    }

    [[nodiscard]] BeatGridPosition position_at_frame(
        double media_frame) const noexcept {
        return mapper_.position_at_frame(media_frame);
    }

    [[nodiscard]] TransportSyncDecision update(
        const CommRaT::Messages::TransportBlock& transport,
        CommRaT::Messages::TransportSyncMode mode,
        double media_position) const noexcept {
        TransportSyncDecision decision{
            .playing = transport.state == CommRaT::Messages::TRANSPORT_PLAYING
                || transport.state == CommRaT::Messages::TRANSPORT_RECORDING,
        };
        if (!valid_ || !valid_transport(transport)
            || !std::isfinite(media_position) || media_position < 0.0) {
            return decision;
        }
        decision.valid = true;
        if (mode == CommRaT::Messages::TRANSPORT_SYNC_OFF) {
            return decision;
        }

        const double media_tempo = mapper_.tempo_at_frame(media_position);
        const double nominal_rate = transport.tempo_bpm / media_tempo;
        if (!valid_rate(nominal_rate)) {
            decision.valid = false;
            return decision;
        }
        decision.target_rate = nominal_rate;
        decision.action = TransportSyncAction::SetRate;
        if (mode == CommRaT::Messages::TRANSPORT_SYNC_TEMPO) {
            return decision;
        }

        const auto media_beat = mapper_.position_at_frame(media_position);
        if (!media_beat.valid) {
            decision.valid = false;
            return decision;
        }
        decision.phase_error_beats = transport.beat_position
            - media_beat.beat_position;
        const bool discontinuity = (transport.flags
            & (CommRaT::Messages::TRANSPORT_DISCONTINUITY
                | CommRaT::Messages::TRANSPORT_SEEK)) != 0;
        if (discontinuity
            || std::abs(decision.phase_error_beats)
                > settings_.seek_threshold_beats) {
            const double target_frame = mapper_.frame_at_beat(
                transport.beat_position);
            if (!std::isfinite(target_frame) || target_frame < 0.0
                || target_frame > static_cast<double>(UINT64_MAX)) {
                decision.valid = false;
                return decision;
            }
            decision.seek_frame = static_cast<uint64_t>(std::llround(target_frame));
            decision.action = TransportSyncAction::Seek;
            return decision;
        }

        const double nudge = std::clamp(
            decision.phase_error_beats * settings_.phase_gain,
            -settings_.maximum_nudge,
            settings_.maximum_nudge);
        decision.target_rate = std::clamp(
            nominal_rate + nudge,
            settings_.minimum_rate,
            settings_.maximum_rate);
        return decision;
    }

private:
    static bool valid_settings(const Settings& settings) noexcept {
        return std::isfinite(settings.minimum_rate)
            && std::isfinite(settings.maximum_rate)
            && std::isfinite(settings.phase_gain)
            && std::isfinite(settings.maximum_nudge)
            && std::isfinite(settings.seek_threshold_beats)
            && settings.minimum_rate > 0.0
            && settings.minimum_rate <= settings.maximum_rate
            && settings.phase_gain >= 0.0
            && settings.maximum_nudge >= 0.0
            && settings.seek_threshold_beats > 0.0;
    }

    [[nodiscard]] bool valid_rate(double rate) const noexcept {
        return std::isfinite(rate)
            && rate >= settings_.minimum_rate
            && rate <= settings_.maximum_rate;
    }

    static bool valid_transport(
        const CommRaT::Messages::TransportBlock& transport) noexcept {
        return std::isfinite(transport.beat_position)
            && transport.beat_position >= 0.0
            && std::isfinite(transport.tempo_bpm)
            && transport.tempo_bpm > 0.0
            && transport.sample_rate_hz > 0
            && transport.beats_per_bar > 0
            && transport.beat_unit > 0;
    }

    BeatGridMapper mapper_;
    Settings settings_{};
    bool valid_{false};
};

} // namespace musicrat::dsp