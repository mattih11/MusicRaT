#pragma once

#include <musicrat/backends/media/decoder_dispatcher.hpp>
#include <musicrat/backends/media/file_identity.hpp>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <sertial/containers/fixed_string.hpp>

namespace musicrat::backends::media {

struct WaveformPeak {
    float minimum{0};
    float maximum{0};
    float rms{0};
    std::array<float, 3> band_rms{};
};

struct WaveformLevel {
    uint64_t frames_per_bin{0};
    std::vector<WaveformPeak> peaks{};
};

struct WaveformViewport {
    double start_frame{0};
    double span_frames{0};
    [[nodiscard]] double position(double frame) const noexcept;
    [[nodiscard]] WaveformViewport pixel_aligned(uint32_t width) const noexcept;
};

struct WaveformTimeTick {
    double frame{0};
    double seconds{0};
};

struct WaveformTimeRuler {
    std::array<WaveformTimeTick, 5> ticks{};
    uint32_t count{0};
};

[[nodiscard]] WaveformTimeRuler waveform_time_ruler(
    const WaveformViewport& viewport, uint32_t sample_rate_hz, uint64_t frame_count) noexcept;

enum class WaveformColorMode { Monochrome, ThreeBand, Rgb };

[[nodiscard]] uint32_t waveform_rgb(const WaveformPeak& peak) noexcept;
[[nodiscard]] uint32_t waveform_band_color(const WaveformPeak& peak, float amplitude) noexcept;

struct WaveformOverview {
    static constexpr uint32_t MAX_BINS = 1024;
    static constexpr uint32_t MAX_DETAIL_BINS = 65536;
    static constexpr uint32_t MIN_DETAIL_FRAMES = 64;
    static constexpr uint32_t ANALYSIS_REVISION = 2;
    static constexpr double LOW_CROSSOVER_HZ = 250;
    static constexpr double HIGH_CROSSOVER_HZ = 2500;
    MediaMetadata media{};
    FileIdentity asset{};
    uint32_t bin_count{0};
    std::array<WaveformPeak, MAX_BINS> peaks{};
    std::vector<WaveformLevel> detail{};

    [[nodiscard]] uint64_t boundary(uint32_t bin) const noexcept;
    [[nodiscard]] WaveformPeak column(uint32_t index, uint32_t width) const noexcept;
    [[nodiscard]] WaveformPeak detail_column(
        const WaveformViewport& viewport, uint32_t index, uint32_t width) const noexcept;
    [[nodiscard]] uint32_t detail_level(double frames_per_pixel) const noexcept;
};

enum class WaveformState { Unavailable, Analyzing, Ready, Failed };
enum class WaveformError {
    None, InvalidRequest, FileUnavailable, AssetChanged, OpenFailed,
    MetadataMismatch, DecodeFailed, InvalidSamples, ResourceExhausted
};

struct WaveformRequest {
    uint64_t track_instance_id{0};
    sertial::fixed_string<512> path{};
    uint64_t frame_count{0};
    uint32_t sample_rate_hz{0};
    uint16_t channel_count{0};
    std::optional<FileIdentity> expected_asset{};
    bool operator==(const WaveformRequest&) const = default;
};

struct WaveformResult {
    WaveformRequest request{};
    WaveformState state{WaveformState::Unavailable};
    WaveformError error{WaveformError::None};
    DecodeError decode_error{DecodeError::None};
    uint64_t analyzed_frames{0};
    bool cache_hit{false};
    std::shared_ptr<const WaveformOverview> overview{};
};

// Ordinary-thread API only. Latest request replaces pending work; one asset is cached.
class WaveformWorker {
public:
    WaveformWorker();
    ~WaveformWorker();
    void submit(const WaveformRequest& request);
    [[nodiscard]] bool poll(WaveformResult& result);

private:
    void run(std::stop_token stop);
    void analyze(const WaveformRequest& request, uint64_t serial, std::stop_token stop);
    void publish(const WaveformResult& result, uint64_t serial);

    std::mutex mutex_;
    std::condition_variable_any wake_;
    WaveformRequest pending_{};
    WaveformResult result_{};
    bool pending_ready_{false}, result_ready_{false};
    std::atomic<uint64_t> serial_{0};
    sertial::fixed_string<512> cached_path_{};
    std::shared_ptr<const WaveformOverview> cached_{};
    std::jthread thread_;
};

[[nodiscard]] const char* waveform_error_text(WaveformError error) noexcept;

} // namespace musicrat::backends::media
