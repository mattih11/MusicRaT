#pragma once

#include <musicrat/protocol/audio_block.hpp>

#include <cstdint>
#include <memory>

namespace musicrat::backends::audio {

struct AudioOutputConfig {
    const char* device_name{nullptr};
    uint32_t sample_rate_hz{0};
    uint16_t channel_count{0};
    uint32_t period_frames{0};
};

class AudioOutputBackend {
public:
    virtual ~AudioOutputBackend() = default;

    [[nodiscard]] virtual const char* name() const noexcept = 0;
    [[nodiscard]] virtual bool open(const AudioOutputConfig& config) noexcept = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual bool write(
        const CommRaT::Messages::AudioBlock& block) noexcept = 0;
    [[nodiscard]] virtual uint64_t underrun_count() const noexcept = 0;
    [[nodiscard]] virtual uint64_t underrun_frame_count() const noexcept = 0;
    [[nodiscard]] virtual uint64_t stream_failure_count() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<AudioOutputBackend>
make_audio_output_backend();

} // namespace musicrat::backends::audio