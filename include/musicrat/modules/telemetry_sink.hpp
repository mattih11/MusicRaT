#pragma once

#include <musicrat/musicrat.hpp>

#include <atomic>
#include <cstdint>

namespace CommRaT {

class TelemetrySink : public MusicRaT::Module2<
    commrat::Input<Messages::LevelMeterBlock>
> {
    using Base = MusicRaT::Module2<commrat::Input<Messages::LevelMeterBlock>>;

public:
    explicit TelemetrySink(const commrat::ModuleConfig& config)
        : Base(config) {}

    [[nodiscard]] uint64_t snapshots_received() const noexcept {
        return snapshots_received_.load(std::memory_order_relaxed);
    }

protected:
    void process(const Messages::LevelMeterBlock&) override {
        snapshots_received_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> snapshots_received_{0};
};

} // namespace CommRaT