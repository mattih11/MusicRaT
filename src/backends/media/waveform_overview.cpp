#include <musicrat/backends/media/waveform_overview.hpp>
#include <musicrat/backends/media/media_decoder.hpp>
#include <musicrat/utility/audio_block.hpp>
#include <musicrat/dsp/biquad.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <string_view>

namespace musicrat::backends::media {
namespace {
bool matches(const MediaMetadata& media, const WaveformRequest& request) {
    return media.frame_count == request.frame_count
        && media.sample_rate_hz == request.sample_rate_hz
        && media.channel_count == request.channel_count;
}

void merge_extrema(WaveformPeak& peak, const WaveformPeak& other) {
    peak.minimum = std::min(peak.minimum, other.minimum);
    peak.maximum = std::max(peak.maximum, other.maximum);
    peak.rms = std::max(peak.rms, other.rms);
    for (uint32_t band = 0; band < 3; ++band) {
        peak.band_rms[band] = std::max(peak.band_rms[band], other.band_rms[band]);
    }
}

struct Energy {
    double full{0};
    std::array<double, 3> bands{};
    void finish(WaveformPeak& peak, double samples) const {
        peak.rms = static_cast<float>(std::sqrt(full / samples));
        for (uint32_t band = 0; band < 3; ++band) {
            peak.band_rms[band] = static_cast<float>(std::sqrt(bands[band] / samples));
        }
    }
};

struct Bands {
    dsp::BiquadSection<double> low, mid_high, mid_low, high;
    void configure(double sample_rate) {
        constexpr double q = 0.7071067811865476;
        const auto low_cut = dsp::bounded_frequency(WaveformOverview::LOW_CROSSOVER_HZ, sample_rate);
        const auto high_cut = dsp::bounded_frequency(WaveformOverview::HIGH_CROSSOVER_HZ, sample_rate);
        low.set_coefficients(dsp::make_biquad_coefficients(dsp::BiquadType::LowPass, sample_rate, low_cut, q, 0));
        mid_high.set_coefficients(dsp::make_biquad_coefficients(dsp::BiquadType::HighPass, sample_rate, low_cut, q, 0));
        mid_low.set_coefficients(dsp::make_biquad_coefficients(dsp::BiquadType::LowPass, sample_rate, high_cut, q, 0));
        high.set_coefficients(dsp::make_biquad_coefficients(dsp::BiquadType::HighPass, sample_rate, high_cut, q, 0));
    }
    std::array<double, 3> process(double sample) {
        return {low.process(sample), mid_low.process(mid_high.process(sample)), high.process(sample)};
    }
};
} // namespace

double WaveformViewport::position(double frame) const noexcept {
    return span_frames > 0 && std::isfinite(span_frames) && std::isfinite(start_frame)
        && std::isfinite(frame) ? (frame - start_frame) / span_frames : -1;
}

WaveformViewport WaveformViewport::pixel_aligned(uint32_t width) const noexcept {
    if (width == 0 || span_frames <= 0 || !std::isfinite(span_frames)
        || !std::isfinite(start_frame)) return {};
    const double per_pixel = span_frames / width;
    const double cell = std::round(start_frame / per_pixel);
    if (per_pixel <= 0 || !std::isfinite(cell) || !std::isfinite(cell * per_pixel)) return {};
    return {.start_frame = cell * per_pixel, .span_frames = span_frames};
}

WaveformTimeRuler waveform_time_ruler(
    const WaveformViewport& viewport, uint32_t sample_rate_hz, uint64_t frame_count) noexcept {
    WaveformTimeRuler ruler{};
    if (sample_rate_hz == 0 || frame_count == 0 || viewport.span_frames <= 0
        || !std::isfinite(viewport.span_frames) || !std::isfinite(viewport.start_frame)) return ruler;
    const double step = viewport.span_frames / 4;
    const double first = std::ceil(std::max(0.0, viewport.start_frame) / step);
    if (step <= 0 || !std::isfinite(first)) return ruler;
    for (uint32_t index = 0; index < ruler.ticks.size(); ++index) {
        const double frame = (first + index) * step;
        const double position = viewport.position(frame);
        if (frame > static_cast<double>(frame_count) || position > 1) break;
        if (!std::isfinite(position) || position < 0) continue;
        ruler.ticks[ruler.count++] = {.frame = frame, .seconds = frame / sample_rate_hz};
    }
    return ruler;
}

uint32_t waveform_rgb(const WaveformPeak& peak) noexcept {
    const auto maximum = *std::max_element(peak.band_rms.begin(), peak.band_rms.end());
    if (!std::isfinite(maximum) || maximum < 0.00001F) return 0x536170;
    uint32_t color = 0;
    for (const auto energy : peak.band_rms) {
        const auto component = static_cast<uint32_t>(std::lround(255 * std::clamp(energy / maximum, 0.0F, 1.0F)));
        color = (color << 8) | component;
    }
    return color;
}

uint32_t waveform_band_color(const WaveformPeak& peak, float amplitude) noexcept {
    uint32_t color = 0;
    for (const auto energy : peak.band_rms) {
        color = (color << 8) | (energy > 0.00001F && energy >= amplitude ? 255U : 0U);
    }
    return color;
}

uint64_t WaveformOverview::boundary(uint32_t bin) const noexcept {
    if (bin_count == 0) return 0;
    const auto index = std::min(bin, bin_count);
    return (media.frame_count / bin_count) * index
        + ((media.frame_count % bin_count) * index) / bin_count;
}

WaveformPeak WaveformOverview::column(uint32_t index, uint32_t width) const noexcept {
    if (width == 0 || index >= width || bin_count == 0 || bin_count > MAX_BINS) return {};
    const auto first = static_cast<uint32_t>(static_cast<uint64_t>(index) * bin_count / width);
    const auto end = static_cast<uint32_t>(
        (static_cast<uint64_t>(index + 1) * bin_count + width - 1) / width);
    auto peak = peaks[first];
    for (auto bin = first + 1; bin < end; ++bin) {
        merge_extrema(peak, peaks[bin]);
    }
    return peak;
}

uint32_t WaveformOverview::detail_level(double frames_per_pixel) const noexcept {
    uint32_t level = 0;
    while (level + 1 < detail.size() && static_cast<double>(detail[level + 1].frames_per_bin) <= frames_per_pixel) {
        ++level;
    }
    return level;
}

WaveformPeak WaveformOverview::detail_column(
    const WaveformViewport& viewport, uint32_t index, uint32_t width) const noexcept {
    if (detail.empty() || width == 0 || index >= width || viewport.span_frames <= 0
        || !std::isfinite(viewport.span_frames) || !std::isfinite(viewport.start_frame)) return {};
    const double per_pixel = viewport.span_frames / width;
    // Keep source cells invariant under scrolling, including floating-point rounding at bin edges.
    const double cell = std::round(viewport.start_frame / per_pixel) + index;
    const double begin = cell * per_pixel;
    const double end = (cell + 1) * per_pixel;
    if (per_pixel <= 0 || !std::isfinite(begin) || !std::isfinite(end)) return {};
    const double duration = static_cast<double>(media.frame_count);
    if (end <= 0 || begin >= duration) return {};
    const auto& level = detail[detail_level(per_pixel)];
    if (level.frames_per_bin == 0 || level.peaks.empty()) return {};
    const auto first = static_cast<std::size_t>(std::clamp(
        std::floor(std::max(0.0, begin) / static_cast<double>(level.frames_per_bin)),
        0.0, static_cast<double>(level.peaks.size() - 1)));
    const auto last = static_cast<std::size_t>(std::clamp(
        std::ceil(std::min(duration, end) / static_cast<double>(level.frames_per_bin)),
        static_cast<double>(first + 1), static_cast<double>(level.peaks.size())));
    auto peak = level.peaks[first];
    for (auto bin = first + 1; bin < last; ++bin) merge_extrema(peak, level.peaks[bin]);
    return peak;
}

WaveformWorker::WaveformWorker()
    : thread_([this](std::stop_token stop) { run(stop); }) {}

WaveformWorker::~WaveformWorker() {
    thread_.request_stop();
    wake_.notify_all();
    thread_.join();
}

void WaveformWorker::submit(const WaveformRequest& request) {
    std::lock_guard lock{mutex_};
    serial_.fetch_add(1, std::memory_order_release);
    pending_ = request;
    pending_ready_ = true;
    result_ = {.request = request, .state = request.track_instance_id != 0
        ? WaveformState::Analyzing : WaveformState::Unavailable};
    result_ready_ = true;
    wake_.notify_all();
}

bool WaveformWorker::poll(WaveformResult& result) {
    std::lock_guard lock{mutex_};
    if (!result_ready_) return false;
    result = result_;
    result_ready_ = false;
    return true;
}

void WaveformWorker::publish(const WaveformResult& result, uint64_t serial) {
    std::lock_guard lock{mutex_};
    if (serial != serial_.load(std::memory_order_acquire)) return;
    result_ = result;
    result_ready_ = true;
}

void WaveformWorker::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        WaveformRequest request{};
        uint64_t serial{};
        {
            std::unique_lock lock{mutex_};
            if (!wake_.wait(lock, stop, [this] { return pending_ready_; })) return;
            request = pending_;
            pending_ready_ = false;
            serial = serial_.load(std::memory_order_acquire);
        }
        try {
            analyze(request, serial, stop);
        } catch (const std::bad_alloc&) {
            publish({.request = request, .state = WaveformState::Failed,
                .error = WaveformError::ResourceExhausted}, serial);
        }
    }
}

void WaveformWorker::analyze(const WaveformRequest& request, uint64_t serial, std::stop_token stop) {
    if (request.track_instance_id == 0) return;
    WaveformResult result{.request = request, .state = WaveformState::Failed};
    const auto fail = [&](WaveformError error, DecodeError decode = DecodeError::None) {
        result.state = WaveformState::Failed;
        result.error = error;
        result.decode_error = decode;
        publish(result, serial);
    };
    const auto cancelled = [&] {
        return stop.stop_requested() || serial_.load(std::memory_order_acquire) != serial;
    };
    if (request.path.empty()
        || std::string_view{request.path.c_str(), request.path.size()}.find('\0') != std::string_view::npos
        || request.frame_count == 0 || request.sample_rate_hz == 0
        || request.channel_count == 0 || request.channel_count > CommRaT::Messages::AudioBlock::MAX_CHANNELS) {
        fail(WaveformError::InvalidRequest);
        return;
    }
    const auto before = regular_file_identity(request.path.c_str());
    if (!before) {
        fail(WaveformError::FileUnavailable);
        return;
    }
    if (request.expected_asset && *request.expected_asset != *before) {
        fail(WaveformError::AssetChanged);
        return;
    }
    if (cached_ && cached_path_ == request.path && cached_->asset == *before
        && matches(cached_->media, request)) {
        result.state = WaveformState::Ready;
        result.cache_hit = true;
        result.analyzed_frames = request.frame_count;
        result.overview = cached_;
        publish(result, serial);
        return;
    }
    auto decoder = std::make_unique<MediaDecoder>();
    if (!decoder->open(request.path.c_str())) {
        fail(WaveformError::OpenFailed, decoder->last_error());
        return;
    }
    if (cancelled()) return;
    if (!matches(decoder->metadata(), request)) {
        fail(WaveformError::MetadataMismatch);
        return;
    }
    auto overview = std::make_shared<WaveformOverview>();
    overview->media = decoder->metadata();
    overview->asset = *before;
    overview->bin_count = static_cast<uint32_t>(std::min<uint64_t>(request.frame_count, WaveformOverview::MAX_BINS));
    for (uint32_t bin = 0; bin < overview->bin_count; ++bin) {
        overview->peaks[bin].minimum = std::numeric_limits<float>::infinity();
        overview->peaks[bin].maximum = -std::numeric_limits<float>::infinity();
    }
    const auto frames_per_bin = std::max<uint64_t>(WaveformOverview::MIN_DETAIL_FRAMES,
        request.frame_count / WaveformOverview::MAX_DETAIL_BINS
            + (request.frame_count % WaveformOverview::MAX_DETAIL_BINS != 0));
    const auto detail_bins = request.frame_count / frames_per_bin + (request.frame_count % frames_per_bin != 0);
    overview->detail.push_back({.frames_per_bin = frames_per_bin,
        .peaks = std::vector<WaveformPeak>(static_cast<std::size_t>(detail_bins))});
    auto& base = overview->detail.front();
    for (auto& peak : base.peaks) {
        peak.minimum = std::numeric_limits<float>::infinity();
        peak.maximum = -std::numeric_limits<float>::infinity();
    }
    std::array<Energy, WaveformOverview::MAX_BINS> energy{};
    Energy detail_energy{};
    std::array<Bands, CommRaT::Messages::AudioBlock::MAX_CHANNELS> filters{};
    for (uint16_t channel = 0; channel < request.channel_count; ++channel) {
        filters[channel].configure(request.sample_rate_hz);
    }
    auto block = std::make_unique<CommRaT::Messages::AudioBlock>();
    uint64_t frames = 0;
    uint32_t bin = 0;
    uint32_t blocks = 0;
    while (!cancelled()) {
        const auto decoded = decoder->read(*block, CommRaT::Messages::AudioBlock::MAX_FRAMES);
        if (decoded == DecodeResult::Error) {
            fail(WaveformError::DecodeFailed, decoder->last_error());
            return;
        }
        if (decoded == DecodeResult::EndOfStream) break;
        if (musicrat::validate_audio_block(*block) != musicrat::AudioBlockValidationError::None
            || block->frame_count == 0 || block->channel_count != request.channel_count
            || block->sample_rate_hz != request.sample_rate_hz
            || block->frame_count > request.frame_count - frames) {
            fail(WaveformError::MetadataMismatch);
            return;
        }
        for (uint32_t frame = 0; frame < block->frame_count; ++frame, ++frames) {
            while (bin + 1 < overview->bin_count && frames >= overview->boundary(bin + 1)) ++bin;
            auto& peak = overview->peaks[bin];
            auto& detailed = base.peaks[static_cast<std::size_t>(frames / frames_per_bin)];
            for (uint16_t channel = 0; channel < request.channel_count; ++channel) {
                const double sample = block->channels[channel][frame];
                if (!std::isfinite(sample) || std::abs(sample) > std::numeric_limits<float>::max()) {
                    fail(WaveformError::InvalidSamples);
                    return;
                }
                peak.minimum = std::min(peak.minimum, static_cast<float>(sample));
                peak.maximum = std::max(peak.maximum, static_cast<float>(sample));
                detailed.minimum = std::min(detailed.minimum, static_cast<float>(sample));
                detailed.maximum = std::max(detailed.maximum, static_cast<float>(sample));
                energy[bin].full += sample * sample;
                detail_energy.full += sample * sample;
                const auto bands = filters[channel].process(sample);
                for (uint32_t band = 0; band < 3; ++band) {
                    if (!std::isfinite(bands[band])) {
                        fail(WaveformError::InvalidSamples);
                        return;
                    }
                    energy[bin].bands[band] += bands[band] * bands[band];
                    detail_energy.bands[band] += bands[band] * bands[band];
                }
            }
            if ((frames + 1) % frames_per_bin == 0 || frames + 1 == request.frame_count) {
                detail_energy.finish(detailed, static_cast<double>(frames % frames_per_bin + 1) * request.channel_count);
                detail_energy = {};
            }
        }
        if (++blocks % 64 == 0) {
            result.state = WaveformState::Analyzing;
            result.analyzed_frames = frames;
            publish(result, serial);
        }
    }
    if (cancelled()) return;
    if (frames != request.frame_count) {
        fail(WaveformError::MetadataMismatch);
        return;
    }
    const auto after = regular_file_identity(request.path.c_str());
    if (!after || after != before) {
        fail(WaveformError::AssetChanged);
        return;
    }
    for (uint32_t index = 0; index < overview->bin_count; ++index) {
        const double count = static_cast<double>(overview->boundary(index + 1) - overview->boundary(index))
            * request.channel_count;
        energy[index].finish(overview->peaks[index], count);
    }
    while (overview->detail.back().peaks.size() > 1) {
        if (cancelled()) return;
        const auto& previous = overview->detail.back();
        const auto span = previous.frames_per_bin;
        WaveformLevel next{.frames_per_bin = span > UINT64_MAX / 2 ? UINT64_MAX : span * 2};
        next.peaks.reserve((previous.peaks.size() + 1) / 2);
        for (std::size_t index = 0; index < previous.peaks.size(); index += 2) {
            auto peak = previous.peaks[index];
            if (index + 1 < previous.peaks.size()) {
                const auto& other = previous.peaks[index + 1];
                const auto left_frames = std::min<uint64_t>(span, request.frame_count - index * span);
                const auto right_frames = std::min<uint64_t>(span, request.frame_count - (index + 1) * span);
                const auto combine = [left_frames, right_frames](float left, float right) {
                    return static_cast<float>(std::sqrt(
                        (static_cast<double>(left) * left * static_cast<double>(left_frames)
                            + static_cast<double>(right) * right * static_cast<double>(right_frames))
                        / (static_cast<double>(left_frames) + static_cast<double>(right_frames))));
                };
                peak.minimum = std::min(peak.minimum, other.minimum);
                peak.maximum = std::max(peak.maximum, other.maximum);
                peak.rms = combine(peak.rms, other.rms);
                for (uint32_t band = 0; band < 3; ++band) {
                    peak.band_rms[band] = combine(peak.band_rms[band], other.band_rms[band]);
                }
            }
            next.peaks.push_back(peak);
        }
        overview->detail.push_back(std::move(next));
    }
    cached_path_ = request.path;
    cached_ = std::move(overview);
    result.state = WaveformState::Ready;
    result.analyzed_frames = frames;
    result.overview = cached_;
    publish(result, serial);
}

const char* waveform_error_text(WaveformError error) noexcept {
    switch (error) {
    case WaveformError::None: return "none";
    case WaveformError::InvalidRequest: return "invalid track metadata/path";
    case WaveformError::FileUnavailable: return "file unavailable/not regular";
    case WaveformError::AssetChanged: return "file changed during analysis";
    case WaveformError::OpenFailed: return "decoder open failed";
    case WaveformError::MetadataMismatch: return "decoded metadata/length differs from player";
    case WaveformError::DecodeFailed: return "decode failed";
    case WaveformError::InvalidSamples: return "nonfinite/out-of-range samples";
    case WaveformError::ResourceExhausted: return "analysis memory exhausted";
    }
    return "invalid waveform error";
}
} // namespace musicrat::backends::media
