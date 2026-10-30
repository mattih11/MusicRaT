#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace musicrat::dsp {

struct AdsrParameters {
    double attack_ms{5.0};
    double decay_ms{0.0};
    double sustain_level{1.0};
    double release_ms{5.0};
};

class AdsrEnvelope {
public:
    void start(
        const AdsrParameters& parameters,
        double sample_rate_hz,
        double peak = 1.0) noexcept {
        parameters_ = parameters;
        sample_rate_hz_ = sample_rate_hz;
        peak_ = peak;
        level_ = 0.0;
        stage_ = Stage::Attack;
        stage_frame_ = 0;
        update_stage_target();
    }

    void release() noexcept {
        if (stage_ == Stage::Idle || stage_ == Stage::Release) {
            return;
        }
        stage_ = Stage::Release;
        stage_frame_ = 0;
        stage_start_ = level_;
        stage_target_ = 0.0;
        stage_frames_ = frames(parameters_.release_ms);
        if (stage_frames_ == 0 || stage_start_ <= 0.0) {
            finish();
        }
    }

    [[nodiscard]] double next() noexcept {
        const double output = level_;
        switch (stage_) {
        case Stage::Attack:
        case Stage::Decay:
        case Stage::Release:
            advance_ramp();
            break;
        case Stage::Sustain:
        case Stage::Idle:
            break;
        }
        return output;
    }

    [[nodiscard]] bool active() const noexcept {
        return stage_ != Stage::Idle;
    }

    [[nodiscard]] double level() const noexcept {
        return level_;
    }

    void reset() noexcept {
        finish();
    }

private:
    enum class Stage : uint8_t {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release,
    };

    [[nodiscard]] uint32_t frames(double milliseconds) const noexcept {
        return static_cast<uint32_t>(std::max(
            0.0, std::round(milliseconds * sample_rate_hz_ / 1000.0)));
    }

    void update_stage_target() noexcept {
        stage_frame_ = 0;
        switch (stage_) {
        case Stage::Attack:
            stage_start_ = level_;
            stage_target_ = peak_;
            stage_frames_ = frames(parameters_.attack_ms);
            break;
        case Stage::Decay:
            stage_start_ = level_;
            stage_target_ = peak_ * parameters_.sustain_level;
            stage_frames_ = frames(parameters_.decay_ms);
            break;
        case Stage::Release:
            stage_start_ = level_;
            stage_target_ = 0.0;
            stage_frames_ = frames(parameters_.release_ms);
            break;
        case Stage::Sustain:
            level_ = peak_ * parameters_.sustain_level;
            break;
        case Stage::Idle:
            level_ = 0.0;
            break;
        }
        if ((stage_ == Stage::Attack || stage_ == Stage::Decay)
            && stage_frames_ == 0) {
            level_ = stage_target_;
            advance_stage();
        } else if (stage_ == Stage::Release
            && (stage_frames_ == 0 || stage_start_ <= 0.0)) {
            finish();
        }
    }

    void advance_ramp() noexcept {
        ++stage_frame_;
        const double progress = std::min(
            1.0, static_cast<double>(stage_frame_)
                / static_cast<double>(stage_frames_));
        level_ = stage_start_ + (stage_target_ - stage_start_) * progress;
        if (stage_frame_ >= stage_frames_) {
            advance_stage();
        }
    }

    void advance_stage() noexcept {
        switch (stage_) {
        case Stage::Attack:
            stage_ = Stage::Decay;
            update_stage_target();
            break;
        case Stage::Decay:
            stage_ = Stage::Sustain;
            level_ = peak_ * parameters_.sustain_level;
            break;
        case Stage::Release:
            finish();
            break;
        case Stage::Sustain:
        case Stage::Idle:
            break;
        }
    }

    void finish() noexcept {
        stage_ = Stage::Idle;
        level_ = 0.0;
        stage_frame_ = 0;
        stage_frames_ = 0;
    }

    AdsrParameters parameters_{};
    double sample_rate_hz_{48000.0};
    double peak_{1.0};
    double level_{0.0};
    double stage_start_{0.0};
    double stage_target_{0.0};
    uint32_t stage_frame_{0};
    uint32_t stage_frames_{0};
    Stage stage_{Stage::Idle};
};

} // namespace musicrat::dsp
