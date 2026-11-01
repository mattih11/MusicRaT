#pragma once

#include <musicrat/backends/media/waveform_overview.hpp>
#include <musicrat/dsp/beat_grid.hpp>
#include <musicrat/protocol/playback_status.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace musicrat::backends::media {

struct WaveformBeatTick {
    double frame{0};
    double beat{0};
    bool downbeat{false};
};

struct WaveformBeatLines {
    static constexpr uint32_t MAX_TICKS = 128;
    std::array<WaveformBeatTick, MAX_TICKS> ticks{};
    uint32_t count{0};
    double stride_beats{1};
    bool valid{false};
};

[[nodiscard]] inline bool waveform_grid_available(
    const CommRaT::Messages::PlaybackStatusBlock& status) noexcept {
    return status.track_instance_id != 0
        && status.beat_grid_track_instance_id == status.track_instance_id
        && status.source_sample_rate_hz != 0 && status.duration_frames != 0
        && (status.flags & CommRaT::Messages::PLAYBACK_STATUS_INVALID) == 0
        && ((status.beat_grid.flags & CommRaT::Messages::BEAT_GRID_FLAG_ANALYZED) == 0
            || (status.beat_grid.flags & CommRaT::Messages::BEAT_GRID_FLAG_IMPORTED) != 0)
        && status.beat_grid.sample_rate_hz == status.source_sample_rate_hz
        && dsp::BeatGridMapper{status.beat_grid}.valid();
}

// In-band projection only; the fixed output and minimum spacing bound draw work.
[[nodiscard]] inline WaveformBeatLines waveform_beat_lines(
    const CommRaT::Messages::BeatGrid& grid, const WaveformViewport& viewport,
    uint64_t frame_count, uint32_t width) noexcept {
    WaveformBeatLines result{};
    if (!dsp::BeatGridMapper{grid}.valid() || width < 2 || frame_count == 0
        || !std::isfinite(viewport.start_frame) || !std::isfinite(viewport.span_frames)
        || viewport.span_frames <= 0
        || !std::isfinite(viewport.start_frame + viewport.span_frames)) return result;

    double minimum_frames_per_beat = std::numeric_limits<double>::max();
    for (const auto& segment : grid.segments) {
        const double frames = 60.0 * grid.sample_rate_hz / segment.tempo_bpm;
        if (!std::isfinite(frames) || frames <= 0) return result;
        minimum_frames_per_beat = std::min(minimum_frames_per_beat, frames);
    }
    const double spacing = std::max(12.0, static_cast<double>(width) / (WaveformBeatLines::MAX_TICKS - 2));
    const double required_stride = spacing * viewport.span_frames / (width - 1) / minimum_frames_per_beat;
    constexpr double max_exact_beat = 9007199254740991.0;
    if (!std::isfinite(required_stride) || required_stride > max_exact_beat) return result;
    if (required_stride > 1) {
        result.stride_beats = grid.beats_per_bar;
        while (result.stride_beats < required_stride) result.stride_beats *= 2;
    }
    const double start = std::max(viewport.start_frame, static_cast<double>(grid.segments.front().start_frame));
    const double end = std::min(viewport.start_frame + viewport.span_frames, static_cast<double>(frame_count));
    result.valid = true;
    if (end < start) return result;
    for (std::size_t index = 0; index < grid.segments.size(); ++index) {
        const auto& segment = grid.segments[index];
        const double segment_start = static_cast<double>(segment.start_frame);
        const double next_frame = index + 1 < grid.segments.size()
            ? static_cast<double>(grid.segments[index + 1].start_frame)
            : static_cast<double>(frame_count);
        const double segment_end = std::min(next_frame, static_cast<double>(frame_count));
        if (segment_end <= start || segment_start > end) continue;
        const double frames_per_beat = 60.0 * grid.sample_rate_hz / segment.tempo_bpm;
        const double first_beat = segment.start_beat + (std::max(start, segment_start) - segment_start) / frames_per_beat;
        double beat = std::ceil(first_beat / result.stride_beats) * result.stride_beats;
        if (!std::isfinite(beat) || beat > max_exact_beat) return {};
        for (uint32_t tick = 0; tick <= WaveformBeatLines::MAX_TICKS; ++tick) {
            const double frame = segment_start + (beat - segment.start_beat) * frames_per_beat;
            if (!std::isfinite(frame)) return {};
            if (frame >= segment_end || frame > end) break;
            if (frame >= start) {
                if (result.count == WaveformBeatLines::MAX_TICKS) return {};
                result.ticks[result.count++] = {.frame = frame, .beat = beat,
                    .downbeat = std::fmod(beat, grid.beats_per_bar) == 0};
            }
            if (beat + result.stride_beats > max_exact_beat) return {};
            beat += result.stride_beats;
        }
    }
    return result;
}

} // namespace musicrat::backends::media
