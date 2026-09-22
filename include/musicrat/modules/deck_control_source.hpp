#pragma once

#include <musicrat/musicrat.hpp>

#include <cmath>

namespace CommRaT {

class DeckControlSource : public MusicRaT::Module2<
    commrat::Output<Messages::DeckControlEventBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::DeckControlSource>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::DeckControlEventBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::DeckControlSource>>;

public:
    explicit DeckControlSource(const commrat::ModuleConfig& config)
        : Base(config) {
        normalize_params();
    }

protected:
    void process(Messages::DeckControlEventBlock& output) override {
        output.events.clear();
        output.timestamp_ns = commrat::Time::now();
        output.sequence_number = sequence_number_++;
        if (this->params_.enabled) {
            output.events.push_back(Messages::DeckControlEvent{
                .type = this->params_.type,
                .sample_offset = 0,
                .value = this->params_.value,
                .ramp_frames = this->params_.ramp_frames,
                .quantization = this->params_.quantization,
            });
        }
    }

    void on_params_changed() override {
        normalize_params();
    }

private:
    void normalize_params() noexcept {
        if (this->params_.type > Messages::DECK_CONTROL_DISABLE_LOOP) {
            this->params_.type = Messages::DECK_CONTROL_PLAY;
        }
        if (!std::isfinite(this->params_.value)) {
            this->params_.value = 1.0;
        }
        if (this->params_.quantization > Messages::DECK_QUANTIZE_BAR) {
            this->params_.quantization = Messages::DECK_QUANTIZE_IMMEDIATE;
        }
    }

    uint64_t sequence_number_{0};
};

} // namespace CommRaT