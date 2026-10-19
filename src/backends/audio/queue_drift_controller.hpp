#pragma once

#include <algorithm>
#include <cstdint>

namespace musicrat::backends::audio::detail {

[[nodiscard]] constexpr double source_step_for_correction(
    int32_t correction_ppm) noexcept {
    return 1.0 + static_cast<double>(correction_ppm) / 1'000'000.0;
}

[[nodiscard]] constexpr double linear_interpolate(
    double first, double second, double fraction) noexcept {
    return first + (second - first) * fraction;
}

[[nodiscard]] inline uint64_t advance_source_cursor(
    double phase,
    uint32_t output_frames,
    double source_step,
    double& fractional_phase) noexcept {
    const double advanced_phase =
        phase + static_cast<double>(output_frames) * source_step;
    const uint64_t consumed_frames =
        static_cast<uint64_t>(advanced_phase);
    fractional_phase = advanced_phase - consumed_frames;
    return consumed_frames;
}

class QueueDriftController {
public:
    static constexpr int32_t maximum_correction_ppm = 500;
    static constexpr int32_t correction_gain_denominator = 4;

    void reset(uint64_t target_frames) noexcept {
        target_frames_ = target_frames;
        correction_ppm_ = 0;
    }

    [[nodiscard]] int32_t update(uint64_t queued_frames) noexcept {
        const int64_t error = static_cast<int64_t>(queued_frames)
            - static_cast<int64_t>(target_frames_);
        const int64_t requested = error / correction_gain_denominator;
        correction_ppm_ = static_cast<int32_t>(std::clamp<int64_t>(
            requested, -maximum_correction_ppm, maximum_correction_ppm));
        return correction_ppm_;
    }

    [[nodiscard]] int32_t correction_ppm() const noexcept {
        return correction_ppm_;
    }

    [[nodiscard]] double source_step() const noexcept {
        return source_step_for_correction(correction_ppm_);
    }

private:
    uint64_t target_frames_{0};
    int32_t correction_ppm_{0};
};

} // namespace musicrat::backends::audio::detail
