#include <musicrat/config.hpp>

#include "queue_drift_controller.hpp"
#include "playback_preroll.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

class ClockDriftSimulation {
public:
    static constexpr uint64_t sample_rate_hz = static_cast<uint64_t>(
        musicrat::config::default_sample_rate_hz + 0.5);
    static constexpr uint64_t queue_capacity_frames =
        musicrat::config::max_audio_frames * 4;
    static constexpr uint64_t producer_block_frames =
        std::min<uint64_t>(
            static_cast<uint64_t>(
                sample_rate_hz * musicrat::config::default_block_period_ms
                    / 1000.0
                    + 0.5),
            musicrat::config::max_audio_frames);
    static constexpr uint64_t consumer_period_frames =
        std::min<uint64_t>((sample_rate_hz + 100) / 200,
            queue_capacity_frames / 2);
    static constexpr uint64_t device_buffer_frames = (sample_rate_hz + 25) / 50;
    static constexpr uint64_t maximum_prefill_frames =
        queue_capacity_frames - musicrat::config::max_audio_frames;
    static constexpr uint64_t prefill_frames = std::min(
        maximum_prefill_frames,
        device_buffer_frames
            + 2 * std::max(producer_block_frames, consumer_period_frames));

    explicit ClockDriftSimulation(bool adaptive = false)
        : queued_frames_(prefill_frames),
          high_water_frames_(prefill_frames),
          accepted_frames_(prefill_frames),
          adaptive_(adaptive) {
        preroll_.reset(
            producer_block_frames,
            queue_capacity_frames - musicrat::config::max_audio_frames,
            device_buffer_frames);
        drift_controller_.reset(prefill_frames);
    }

    void advance(uint64_t duration_seconds, int32_t producer_offset_ppm) {
        const uint64_t interval = producer_interval(producer_offset_ppm);
        next_producer_time_ = now_ + interval;
        const uint64_t end_time = now_ + duration_seconds * ticks_per_second;

        while (std::min(next_producer_time_, next_consumer_time_) < end_time) {
            if (next_producer_time_ <= next_consumer_time_) {
                now_ = next_producer_time_;
                next_producer_time_ += interval;
                if (queued_frames_ + producer_block_frames
                    <= queue_capacity_frames) {
                    queued_frames_ += producer_block_frames;
                    accepted_frames_ += producer_block_frames;
                    high_water_frames_ =
                        std::max(high_water_frames_, queued_frames_);
                } else {
                    rejected_blocks_++;
                }
                continue;
            }

            now_ = next_consumer_time_;
            next_consumer_time_ += consumer_interval;
            running_ = preroll_.ready(queued_frames_, consumer_period_frames);
            if (!running_) {
                silent_frames_ += consumer_period_frames;
                continue;
            }
            const int32_t correction_ppm = adaptive_
                ? drift_controller_.update(queued_frames_)
                : 0;
            maximum_abs_correction_ppm_ = std::max(
                maximum_abs_correction_ppm_,
                static_cast<uint32_t>(
                    correction_ppm < 0 ? -correction_ppm : correction_ppm));
            const uint64_t scaled_source_frames =
                source_frame_fraction_
                + consumer_period_frames
                    * static_cast<uint64_t>(1'000'000 + correction_ppm);
            const uint64_t source_frames = scaled_source_frames / 1'000'000;
            if (queued_frames_ < source_frames) {
                const uint64_t missing_output_frames =
                    consumer_period_frames - std::min(
                        consumer_period_frames, queued_frames_);
                missing_frames_ += missing_output_frames;
                silent_frames_ += missing_output_frames;
                consumed_frames_ += queued_frames_;
                queued_frames_ = 0;
                underruns_++;
                running_ = false;
                preroll_.rebuffer();
                continue;
            }
            source_frame_fraction_ = scaled_source_frames % 1'000'000;
            queued_frames_ -= source_frames;
            consumed_frames_ += source_frames;
        }

        now_ = end_time;
    }

    [[nodiscard]] uint64_t queued_frames() const { return queued_frames_; }
    [[nodiscard]] uint64_t high_water_frames() const { return high_water_frames_; }
    [[nodiscard]] uint64_t rejected_blocks() const { return rejected_blocks_; }
    [[nodiscard]] uint64_t underruns() const { return underruns_; }
    [[nodiscard]] uint64_t missing_frames() const { return missing_frames_; }
    [[nodiscard]] uint64_t silent_frames() const { return silent_frames_; }
    [[nodiscard]] uint64_t accepted_frames() const { return accepted_frames_; }
    [[nodiscard]] uint64_t consumed_frames() const { return consumed_frames_; }
    [[nodiscard]] uint32_t maximum_abs_correction_ppm() const {
        return maximum_abs_correction_ppm_;
    }
    [[nodiscard]] int32_t correction_ppm() const {
        return drift_controller_.correction_ppm();
    }
    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] bool started() const { return preroll_.started(); }

private:
    static constexpr uint64_t ticks_per_second = 1'000'000'000'000'000ULL;
    static constexpr uint64_t consumer_interval =
        consumer_period_frames * ticks_per_second / sample_rate_hz;

    static uint64_t producer_interval(int32_t offset_ppm) {
        const uint64_t rate_scale = static_cast<uint64_t>(
            static_cast<int64_t>(1'000'000) + offset_ppm);
        const uint64_t nominal_interval =
            producer_block_frames * ticks_per_second / sample_rate_hz;
        return nominal_interval / rate_scale * 1'000'000
            + (nominal_interval % rate_scale) * 1'000'000 / rate_scale;
    }

    uint64_t now_{0};
    uint64_t next_producer_time_{0};
    uint64_t next_consumer_time_{consumer_interval};
    uint64_t queued_frames_{0};
    uint64_t high_water_frames_{0};
    uint64_t accepted_frames_{0};
    uint64_t consumed_frames_{0};
    uint64_t rejected_blocks_{0};
    uint64_t underruns_{0};
    uint64_t missing_frames_{0};
    uint64_t silent_frames_{0};
    uint64_t source_frame_fraction_{0};
    uint32_t maximum_abs_correction_ppm_{0};
    bool running_{false};
    bool adaptive_{false};
    musicrat::backends::audio::detail::QueueDriftController drift_controller_;
    musicrat::backends::audio::detail::PlaybackPreroll preroll_;
};

void assert_invariants(const ClockDriftSimulation& simulation) {
    assert(simulation.queued_frames() <= ClockDriftSimulation::queue_capacity_frames);
    assert(simulation.high_water_frames() <= ClockDriftSimulation::queue_capacity_frames);
    assert(simulation.high_water_frames() >= simulation.queued_frames());
    assert(simulation.accepted_frames()
        == simulation.consumed_frames() + simulation.queued_frames());
}

} // namespace

int main() {
    using musicrat::backends::audio::detail::QueueDriftController;
    using musicrat::backends::audio::detail::advance_source_cursor;
    using musicrat::backends::audio::detail::linear_interpolate;
    using musicrat::backends::audio::detail::source_step_for_correction;

    assert(linear_interpolate(-1.0, 1.0, 0.25) == -0.5);
    double fractional_phase = 0.0;
    assert(advance_source_cursor(
        0.0, 100, source_step_for_correction(100), fractional_phase) == 100);
    assert(std::abs(fractional_phase - 0.01) < 0.000001);
    assert(advance_source_cursor(
        0.0, 100, source_step_for_correction(-100), fractional_phase) == 99);
    assert(std::abs(fractional_phase - 0.99) < 0.000001);

    QueueDriftController controller;
    controller.reset(ClockDriftSimulation::prefill_frames);
    assert(controller.update(ClockDriftSimulation::prefill_frames) == 0);
    assert(controller.update(ClockDriftSimulation::prefill_frames + 400) == 100);
    assert(controller.update(ClockDriftSimulation::prefill_frames - 400) == -100);
    controller.reset(4096);
    assert(controller.update(4096 + 4 * QueueDriftController::maximum_correction_ppm)
        == QueueDriftController::maximum_correction_ppm);
    assert(controller.update(4096 - 4 * QueueDriftController::maximum_correction_ppm)
        == -QueueDriftController::maximum_correction_ppm);

    ClockDriftSimulation positive;
    positive.advance(3600, 100);
    assert(positive.started());
    assert(positive.rejected_blocks() > 0);
    assert(positive.underruns() == 0);
    assert(positive.missing_frames() == 0);
    assert_invariants(positive);
    std::printf(
        "+100 ppm for 1 h: high-water=%llu/%llu frames, rejected blocks=%llu, "
        "underruns=%llu\n",
        static_cast<unsigned long long>(positive.high_water_frames()),
        static_cast<unsigned long long>(ClockDriftSimulation::queue_capacity_frames),
        static_cast<unsigned long long>(positive.rejected_blocks()),
        static_cast<unsigned long long>(positive.underruns()));

    ClockDriftSimulation negative;
    negative.advance(600, -100);
    assert(negative.started());
    assert(negative.underruns() > 0);
    assert(negative.missing_frames() > 0);
    assert(negative.silent_frames() > 0);
    assert_invariants(negative);
    std::printf(
        "-100 ppm for 10 min: high-water=%llu/%llu frames, underruns=%llu, "
        "missing frames=%llu, rebuffer silence=%llu frames\n",
        static_cast<unsigned long long>(negative.high_water_frames()),
        static_cast<unsigned long long>(ClockDriftSimulation::queue_capacity_frames),
        static_cast<unsigned long long>(negative.underruns()),
        static_cast<unsigned long long>(negative.missing_frames()),
        static_cast<unsigned long long>(negative.silent_frames()));

    const uint64_t underruns_before_resynchronization = negative.underruns();
    negative.advance(120, 0);
    assert(negative.running());
    assert(negative.underruns() == underruns_before_resynchronization);
    assert_invariants(negative);
    std::printf(
        "after 2 min at nominal rate: running=%s, queue=%llu frames, "
        "additional underruns=0\n",
        negative.running() ? "yes" : "no",
        static_cast<unsigned long long>(negative.queued_frames()));

    ClockDriftSimulation adaptive_positive{true};
    adaptive_positive.advance(3600, 100);
    assert(adaptive_positive.started());
    assert(adaptive_positive.rejected_blocks() == 0);
    assert(adaptive_positive.underruns() == 0);
    assert(adaptive_positive.missing_frames() == 0);
    assert(adaptive_positive.maximum_abs_correction_ppm() <=
        QueueDriftController::maximum_correction_ppm);
    assert(adaptive_positive.correction_ppm() >= 50);
    assert(adaptive_positive.correction_ppm() <= 150);
    assert_invariants(adaptive_positive);
    std::printf(
        "adaptive +100 ppm for 1 h: high-water=%llu/%llu frames, "
        "correction=%d ppm (max abs %u), rejected=0, underruns=0\n",
        static_cast<unsigned long long>(adaptive_positive.high_water_frames()),
        static_cast<unsigned long long>(ClockDriftSimulation::queue_capacity_frames),
        adaptive_positive.correction_ppm(),
        adaptive_positive.maximum_abs_correction_ppm());

    ClockDriftSimulation adaptive_negative{true};
    adaptive_negative.advance(600, -100);
    assert(adaptive_negative.started());
    assert(adaptive_negative.rejected_blocks() == 0);
    assert(adaptive_negative.underruns() == 0);
    assert(adaptive_negative.missing_frames() == 0);
    assert(adaptive_negative.maximum_abs_correction_ppm() <=
        QueueDriftController::maximum_correction_ppm);
    assert(adaptive_negative.queued_frames() < ClockDriftSimulation::prefill_frames);
    assert_invariants(adaptive_negative);
    assert(adaptive_negative.correction_ppm() <= -50);
    assert(adaptive_negative.correction_ppm() >= -150);
    const auto correction_before_resynchronization =
        adaptive_negative.maximum_abs_correction_ppm();
    const auto negative_rate_correction_ppm =
        adaptive_negative.correction_ppm();
    adaptive_negative.advance(600, 0);
    assert(adaptive_negative.running());
    assert(adaptive_negative.underruns() == 0);
    assert(adaptive_negative.correction_ppm() >= -100);
    assert(adaptive_negative.correction_ppm() <= 100);
    assert(adaptive_negative.queued_frames()
        + 2 * ClockDriftSimulation::consumer_period_frames
        >= ClockDriftSimulation::prefill_frames);
    assert(adaptive_negative.queued_frames()
        <= ClockDriftSimulation::prefill_frames
            + 2 * ClockDriftSimulation::consumer_period_frames);
    assert_invariants(adaptive_negative);
    std::printf(
        "adaptive -100 ppm for 10 min then nominal 10 min: high-water=%llu/%llu "
        "frames, correction=%d ppm before resync (max abs %u), end correction=%d "
        "ppm, end queue=%llu, "
        "rejected=0, underruns=0\n",
        static_cast<unsigned long long>(adaptive_negative.high_water_frames()),
        static_cast<unsigned long long>(ClockDriftSimulation::queue_capacity_frames),
        negative_rate_correction_ppm,
        correction_before_resynchronization,
        adaptive_negative.correction_ppm(),
        static_cast<unsigned long long>(adaptive_negative.queued_frames()));
}
