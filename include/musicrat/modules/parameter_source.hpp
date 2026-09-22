#pragma once

#include <musicrat/musicrat.hpp>
#include <musicrat/protocol/gain.hpp>

#include <cmath>

namespace CommRaT {

class ParameterSource : public MusicRaT::Module2<
    commrat::Output<Messages::ParameterEventBlock>,
    commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
    commrat::Params<Parameters::ParameterSource>
> {
    using Base = MusicRaT::Module2<
        commrat::Output<Messages::ParameterEventBlock>,
        commrat::Period<commrat::Milliseconds(musicrat::config::default_block_period_ms)>,
        commrat::Params<Parameters::ParameterSource>>;

public:
    explicit ParameterSource(const commrat::ModuleConfig& config)
        : Base(config) {
        normalize_params();
    }

protected:
    void process(Messages::ParameterEventBlock& output) override {
        output.events.clear();
        output.timestamp_ns = commrat::Time::now();
        output.sequence_number = sequence_number_++;
        output.events.push_back(Messages::ParameterEvent{
            .source_endpoint_id = this->params_.source_endpoint_id,
            .parameter_id = this->params_.parameter_id,
            .sample_offset = 0,
            .value = this->params_.value,
        });
    }

    void on_params_changed() override {
        normalize_params();
    }

private:
    void normalize_params() noexcept {
        if (!std::isfinite(this->params_.value)) {
            this->params_.value = 0.0;
        }
    }

    uint64_t sequence_number_{0};
};

} // namespace CommRaT