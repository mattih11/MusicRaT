#pragma once

#include <musicrat/protocol/transport.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace musicrat::dsp {

struct BeatGridPosition {
    double beat_position{0.0};
    double beat_fraction{0.0};
    uint64_t bar{0};
    uint16_t beat_in_bar{0};
    bool valid{false};
};

class BeatGridMapper {
public:
    using BeatGrid = CommRaT::Messages::BeatGrid;
    using BeatGridSegment = CommRaT::Messages::BeatGridSegment;

    explicit BeatGridMapper(const BeatGrid& grid) noexcept
        : grid_(grid)
        , valid_(validate(grid)) {}

    [[nodiscard]] bool valid() const noexcept {
        return valid_;
    }

    [[nodiscard]] BeatGridPosition position_at_frame(
        double media_frame) const noexcept {
        if (!valid_ || !std::isfinite(media_frame) || media_frame < 0.0) {
            return {};
        }
        const auto& segment = segment_for_frame(media_frame);
        const double beat = segment.start_beat
            + (media_frame - static_cast<double>(segment.start_frame))
                * segment.tempo_bpm / (60.0 * grid_.sample_rate_hz);
        if (!std::isfinite(beat) || beat < 0.0) {
            return {};
        }
        const double whole_beat = std::floor(beat);
        const auto beat_index = static_cast<uint64_t>(whole_beat);
        return {
            .beat_position = beat,
            .beat_fraction = beat - whole_beat,
            .bar = beat_index / grid_.beats_per_bar,
            .beat_in_bar = static_cast<uint16_t>(
                beat_index % grid_.beats_per_bar),
            .valid = true,
        };
    }

    [[nodiscard]] double frame_at_beat(double beat_position) const noexcept {
        if (!valid_ || !std::isfinite(beat_position) || beat_position < 0.0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const auto& segment = segment_for_beat(beat_position);
        return static_cast<double>(segment.start_frame)
            + (beat_position - segment.start_beat)
                * 60.0 * grid_.sample_rate_hz / segment.tempo_bpm;
    }

    [[nodiscard]] double tempo_at_frame(double media_frame) const noexcept {
        if (!valid_ || !std::isfinite(media_frame) || media_frame < 0.0) {
            return 0.0;
        }
        return segment_for_frame(media_frame).tempo_bpm;
    }

private:
    static bool validate(const BeatGrid& grid) noexcept {
        if (!std::isfinite(grid.sample_rate_hz) || grid.sample_rate_hz <= 0.0
            || grid.segments.empty()
            || grid.beats_per_bar == 0 || grid.beat_unit == 0) {
            return false;
        }
        for (std::size_t index = 0; index < grid.segments.size(); ++index) {
            const auto& segment = grid.segments[index];
            if (!std::isfinite(segment.start_beat) || segment.start_beat < 0.0
                || !std::isfinite(segment.tempo_bpm)
                || segment.tempo_bpm <= 0.0) {
                return false;
            }
            if (index == 0) {
                if (segment.start_beat != 0.0) {
                    return false;
                }
                continue;
            }
            const auto& previous = grid.segments[index - 1];
            if (segment.start_frame <= previous.start_frame
                || segment.start_beat <= previous.start_beat) {
                return false;
            }
            const double expected_beat = previous.start_beat
                + static_cast<double>(segment.start_frame - previous.start_frame)
                    * previous.tempo_bpm / (60.0 * grid.sample_rate_hz);
            if (std::abs(expected_beat - segment.start_beat) > 1.0e-6) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] const BeatGridSegment& segment_for_frame(
        double media_frame) const noexcept {
        uint32_t selected = 0;
        for (std::size_t index = 1; index < grid_.segments.size(); ++index) {
            if (media_frame < static_cast<double>(grid_.segments[index].start_frame)) {
                break;
            }
            selected = index;
        }
        return grid_.segments[selected];
    }

    [[nodiscard]] const BeatGridSegment& segment_for_beat(
        double beat_position) const noexcept {
        uint32_t selected = 0;
        for (std::size_t index = 1; index < grid_.segments.size(); ++index) {
            if (beat_position < grid_.segments[index].start_beat) {
                break;
            }
            selected = index;
        }
        return grid_.segments[selected];
    }

    BeatGrid grid_{};
    bool valid_{false};
};

} // namespace musicrat::dsp