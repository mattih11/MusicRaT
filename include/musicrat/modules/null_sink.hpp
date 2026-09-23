#pragma once

#include <musicrat/launcher/audio_format_validation.hpp>
#include <musicrat/musicrat.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <atomic>
#include <cstdint>

namespace CommRaT {

class NullSink : public MusicRaT::Module2<
    commrat::Input<Messages::AudioBlock>
> {
    using Base = MusicRaT::Module2<commrat::Input<Messages::AudioBlock>>;

public:
    static musicrat::launcher::DescriptorMetadata descriptor_metadata() {
        using namespace musicrat::launcher;
        DescriptorMetadata metadata{};
        metadata.musicrat_ports.ports = {{
            .id = "audio_in",
            .display_name = "Audio In",
            .direction = PORT_DIRECTION_INPUT,
            .port_index = 0,
            .domain = PORT_DOMAIN_AUDIO,
        }};
        return metadata;
    }

    explicit NullSink(const commrat::ModuleConfig& config)
        : Base(config) {}

    [[nodiscard]] uint64_t blocks_received() const noexcept {
        return blocks_received_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t invalid_blocks() const noexcept {
        return invalid_blocks_.load(std::memory_order_relaxed);
    }

protected:
    void process(const Messages::AudioBlock& input) override {
        blocks_received_.fetch_add(1, std::memory_order_relaxed);
        if (musicrat::validate_audio_block(input)
            != musicrat::AudioBlockValidationError::None) {
            invalid_blocks_.fetch_add(1, std::memory_order_relaxed);
        }
    }

private:
    std::atomic<uint64_t> blocks_received_{0};
    std::atomic<uint64_t> invalid_blocks_{0};
};

} // namespace CommRaT