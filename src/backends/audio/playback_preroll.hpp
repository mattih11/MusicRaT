#pragma once

#include <algorithm>
#include <cstdint>

namespace musicrat::backends::audio::detail {

class PlaybackPreroll {
public:
    void reset(
        uint32_t producer_period, uint64_t maximum_prefill,
        uint64_t device_buffer = 0) noexcept {
        producer_period_ = producer_period;
        maximum_prefill_ = maximum_prefill;
        device_buffer_ = device_buffer;
        running_ = false;
        started_ = false;
    }

    [[nodiscard]] bool ready(
        uint64_t queued_frames, uint64_t consumer_frames) noexcept {
        const uint64_t target = std::min(
            maximum_prefill_,
            device_buffer_ + 2 * std::max<uint64_t>(producer_period_, consumer_frames));
        if (!running_ && queued_frames >= target) {
            running_ = true;
            started_ = true;
        }
        return running_;
    }

    void rebuffer() noexcept {
        running_ = false;
    }

    [[nodiscard]] bool started() const noexcept {
        return started_;
    }

private:
    uint64_t producer_period_{0};
    uint64_t maximum_prefill_{0};
    uint64_t device_buffer_{0};
    bool running_{false};
    bool started_{false};
};

} // namespace musicrat::backends::audio::detail
