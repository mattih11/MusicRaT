#include <musicrat/backends/audio/audio_output_backend.hpp>

namespace musicrat::backends::audio {
namespace {

class UnavailableAudioOutputBackend final : public AudioOutputBackend {
public:
    [[nodiscard]] const char* name() const noexcept override {
        return "none";
    }

    [[nodiscard]] bool open(const AudioOutputConfig&) noexcept override {
        return false;
    }

    void close() noexcept override {}

    [[nodiscard]] bool write(
        const CommRaT::Messages::AudioBlock&) noexcept override {
        return false;
    }

    [[nodiscard]] uint64_t underrun_count() const noexcept override {
        return 0;
    }

    [[nodiscard]] uint64_t underrun_frame_count() const noexcept override {
        return 0;
    }

    [[nodiscard]] uint64_t stream_failure_count() const noexcept override {
        return 0;
    }

    [[nodiscard]] uint64_t queued_frames() const noexcept override {
        return 0;
    }

    [[nodiscard]] uint64_t queue_high_water_frames() const noexcept override {
        return 0;
    }

    [[nodiscard]] uint64_t queue_capacity() const noexcept override {
        return 0;
    }

    [[nodiscard]] int32_t rate_correction_ppm() const noexcept override {
        return 0;
    }

    [[nodiscard]] uint32_t maximum_rate_correction_ppm() const noexcept override {
        return 0;
    }
};

} // namespace

std::unique_ptr<AudioOutputBackend> make_audio_output_backend() {
    return std::make_unique<UnavailableAudioOutputBackend>();
}

} // namespace musicrat::backends::audio