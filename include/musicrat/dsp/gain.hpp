#pragma once

#include <musicrat/config.hpp>

#include <cstddef>

namespace musicrat::dsp {

class Gain {
public:
    using Sample = musicrat::config::sample_type;

    explicit Gain(Sample initial_gain = Sample{1}) noexcept
        : current_gain_(initial_gain)
        , target_gain_(initial_gain) {}

    void set_immediate(Sample gain) noexcept {
        current_gain_ = gain;
        target_gain_ = gain;
        remaining_samples_ = 0;
        increment_ = Sample{0};
    }

    void ramp_to(Sample gain, std::size_t sample_count) noexcept {
        target_gain_ = gain;
        remaining_samples_ = sample_count;
        if (sample_count == 0) {
            set_immediate(gain);
            return;
        }
        increment_ = (target_gain_ - current_gain_) / static_cast<Sample>(sample_count);
    }

    [[nodiscard]] Sample next_gain() noexcept {
        if (remaining_samples_ > 0) {
            current_gain_ += increment_;
            --remaining_samples_;
            if (remaining_samples_ == 0) {
                current_gain_ = target_gain_;
            }
        }
        return current_gain_;
    }

    [[nodiscard]] Sample process(Sample input) noexcept {
        return input * next_gain();
    }

    [[nodiscard]] Sample current_gain() const noexcept {
        return current_gain_;
    }

private:
    Sample current_gain_{Sample{1}};
    Sample target_gain_{Sample{1}};
    Sample increment_{Sample{0}};
    std::size_t remaining_samples_{0};
};

} // namespace musicrat::dsp