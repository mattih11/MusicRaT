#include <musicrat/backends/media/waveform_overview.hpp>
#include <musicrat/backends/media/waveform_beat_grid.hpp>
#include <musicrat/backends/audio/wav_writer.hpp>
#include "../support/directory_fixture.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <cstdio>

namespace {
using namespace musicrat::backends::media;
using AudioBlock = CommRaT::Messages::AudioBlock;

double sample(uint64_t frame) {
    if (frame == 1000) return 1.0;
    if (frame == 4000) return -0.5;
    if (frame == 4102) return 0.25;
    return 0;
}

void write_track(const std::filesystem::path& path, uint64_t frames, double gain = 1) {
    musicrat::backends::audio::WavWriter writer;
    assert(writer.open(path.c_str(), 48000, 2));
    auto block = std::make_unique<AudioBlock>();
    for (uint64_t offset = 0; offset < frames;) {
        *block = {};
        block->sample_rate_hz = 48000;
        block->channel_count = 2;
        block->frame_count = static_cast<uint32_t>(std::min<uint64_t>(101, frames - offset));
        for (uint32_t frame = 0; frame < block->frame_count; ++frame) {
            const auto value = static_cast<AudioBlock::Sample>(sample(offset + frame) * gain);
            block->channels[0].push_back(value);
            block->channels[1].push_back(-value);
        }
        assert(writer.write(*block));
        offset += block->frame_count;
    }
}

WaveformResult await(WaveformWorker& worker, uint64_t track) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    WaveformResult result{};
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.poll(result) && result.request.track_instance_id == track
            && result.state != WaveformState::Analyzing) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    assert(false && "waveform analysis deadline");
    return {};
}

bool near(double actual, double expected) { return std::abs(actual - expected) < 0.0001; }

void check_beat_lines() {
    using namespace CommRaT::Messages;
    BeatGrid grid{};
    grid.segments.push_back({.start_frame = 24000, .tempo_bpm = 120});
    grid.segments.push_back({.start_frame = 120000, .start_beat = 4, .tempo_bpm = 240});
    grid.beats_per_bar = 3;
    const auto ticks = waveform_beat_lines(grid, {.span_frames = 192000}, 192000, 776);
    assert(ticks.valid && ticks.stride_beats == 1 && ticks.count == 10);
    for (uint32_t index = 0; index < ticks.count; ++index) {
        const auto& tick = ticks.ticks[index];
        assert(tick.beat == index && tick.downbeat == (index % 3 == 0));
        assert(tick.frame == (index < 4 ? 24000 + index * 24000 : 120000 + (index - 4) * 12000));
    }
    // The tempo seam is neither duplicated nor omitted; no beats appear in pre-roll or at duration.
    assert(waveform_beat_lines(grid, {.start_frame = -48000, .span_frames = 60000}, 192000, 776).count == 0);
    const auto seam = waveform_beat_lines(grid, {.start_frame = 120000, .span_frames = 12000}, 192000, 776);
    assert(seam.count == 2 && seam.ticks[0].beat == 4 && seam.ticks[1].beat == 5);
    auto fractional = grid;
    fractional.segments[1].start_frame = 108000;
    fractional.segments[1].start_beat = 3.5;
    const auto partial = waveform_beat_lines(fractional, {.start_frame = 96000, .span_frames = 48000}, 192000, 776);
    assert(partial.valid && partial.count == 4);
    assert(partial.ticks[0].beat == 3 && partial.ticks[0].frame == 96000);
    assert(partial.ticks[1].beat == 4 && partial.ticks[1].frame == 114000);
    assert(partial.ticks[2].beat == 5 && partial.ticks[2].frame == 126000);
    assert(partial.ticks[3].beat == 6 && partial.ticks[3].frame == 138000);
    const auto cropped = waveform_beat_lines(grid, {.span_frames = 120000}, 96000, 776);
    assert(cropped.valid && cropped.count == 3);
    assert(cropped.ticks[2].frame == 72000);
    for (const uint32_t rate : {44100U, 48000U, 96000U}) {
        grid.sample_rate_hz = rate;
        grid.segments.clear();
        // All 32 allowed segments, each 4 beats long with continuous variable tempo.
        uint64_t frame = rate;
        for (uint32_t index = 0; index < MAX_BEAT_GRID_SEGMENTS; ++index) {
            const double bpm = index % 2 == 0 ? 120 : 240;
            grid.segments.push_back({.start_frame = frame, .start_beat = index * 4.0, .tempo_bpm = bpm});
            frame += index % 2 == 0 ? rate * 2 : rate;
        }
        for (const uint16_t meter : {3, 4, 7}) {
            grid.beats_per_bar = meter;
            for (const uint32_t width : {2U, 100U, 776U, 1280U}) {
                for (const double seconds : {2.0, 4.0, 8.0, 16.0, 32.0, 7200.0}) {
                    for (const double origin : {-1.0, 0.0, 2.0, 20.25}) {
                        const WaveformViewport view{.start_frame = origin * rate, .span_frames = seconds * rate};
                        const auto marks = waveform_beat_lines(grid, view, uint64_t{rate} * 7200, width);
                        assert(marks.valid && marks.count <= WaveformBeatLines::MAX_TICKS);
                        for (uint32_t tick = 0; tick < marks.count; ++tick) {
                            const auto& mark = marks.ticks[tick];
                            assert(mark.frame >= view.start_frame && mark.frame <= view.start_frame + view.span_frames);
                            assert(mark.frame >= rate && mark.frame < rate * 7200.0);
                            assert(near(musicrat::dsp::BeatGridMapper{grid}.frame_at_beat(mark.beat), mark.frame));
                            assert(std::fmod(mark.beat, marks.stride_beats) == 0);
                            if (tick != 0) {
                                assert(mark.frame > marks.ticks[tick - 1].frame);
                                const double distance = (mark.frame - marks.ticks[tick - 1].frame) / view.span_frames * (width - 1);
                                assert(distance >= 11.9999);
                            }
                            if (marks.stride_beats > 1) assert(mark.downbeat);
                        }
                    }
                }
            }
        }
    }
    grid = {};
    grid.segments.push_back({.tempo_bpm = 120});
    const WaveformViewport base{.start_frame = 12000, .span_frames = 192000};
    const auto aligned = base.pixel_aligned(776);
    const auto reference = waveform_beat_lines(grid, aligned, 480000, 776);
    for (const double fraction : {-0.3, 0.0, 0.3}) {
        const auto moving = WaveformViewport{.start_frame = aligned.start_frame + fraction * base.span_frames / 776,
            .span_frames = base.span_frames}.pixel_aligned(776);
        const auto projected = waveform_beat_lines(grid, moving, 480000, 776);
        assert(projected.count == reference.count);
        for (uint32_t index = 0; index < projected.count; ++index) {
            assert(projected.ticks[index].frame == reference.ticks[index].frame);
            assert(moving.position(projected.ticks[index].frame) == aligned.position(reference.ticks[index].frame));
        }
    }
    assert(!waveform_beat_lines({}, base, 480000, 776).valid);
    assert(!waveform_beat_lines(grid, base, 480000, 0).valid);
    assert(!waveform_beat_lines(grid, base, 0, 776).valid);
    assert(!waveform_beat_lines(grid, {}, 480000, 776).valid);
    assert(!waveform_beat_lines(grid, {.span_frames = std::numeric_limits<double>::infinity()}, 480000, 776).valid);
    grid.segments[0].tempo_bpm = std::numeric_limits<double>::max();
    assert(!waveform_beat_lines(grid, base, 480000, 776).valid);

    PlaybackStatusBlock status{};
    status.track_instance_id = status.beat_grid_track_instance_id = 1;
    status.source_sample_rate_hz = 48000;
    status.duration_frames = 480000;
    status.beat_grid.segments.push_back({.tempo_bpm = 120});
    assert(waveform_grid_available(status));
    ++status.beat_grid_track_instance_id;
    assert(!waveform_grid_available(status));
    --status.beat_grid_track_instance_id;
    status.beat_grid.sample_rate_hz = 44100;
    assert(!waveform_grid_available(status));
    status.beat_grid.sample_rate_hz = 48000;
    status.beat_grid.flags = BEAT_GRID_FLAG_ANALYZED;
    assert(!waveform_grid_available(status));
    status.beat_grid.flags |= BEAT_GRID_FLAG_IMPORTED;
    assert(waveform_grid_available(status));
    status.beat_grid.flags = 0;
    status.flags = PLAYBACK_STATUS_INVALID;
    assert(!waveform_grid_available(status));
}

void check_scroll_stability() {
    WaveformOverview waveform{};
    waveform.media.frame_count = 64 * 1024;
    waveform.detail.push_back({.frames_per_bin = 64});
    for (uint32_t bin = 0; bin < 1024; ++bin) {
        const auto value = static_cast<float>((bin * 17) % 31 + 1) / 32;
        waveform.detail.front().peaks.push_back({.minimum = -value, .maximum = value,
            .rms = value / 2, .band_rms = {value / 3, 0.5F - value / 4, value / 5}});
    }
    const auto same = [](const WaveformPeak& left, const WaveformPeak& right) {
        assert(left.minimum == right.minimum && left.maximum == right.maximum && left.rms == right.rms);
        assert(left.band_rms == right.band_rms && waveform_rgb(left) == waveform_rgb(right));
    };
    for (const uint32_t width : {776U, 800U, 1280U}) {
        for (const double per_pixel : {3.7, 64.0, 157.3, 2048.0}) {
            for (const double origin : {-50.0, 0.0, 35.0}) {
                const WaveformViewport base{.start_frame = origin * per_pixel,
                    .span_frames = width * per_pixel};
                for (const double fraction : {-0.49, -0.3, 0.0, 0.2, 0.49}) {
                    const WaveformViewport moving{.start_frame = base.start_frame + fraction * per_pixel,
                        .span_frames = base.span_frames};
                    const auto aligned = moving.pixel_aligned(width);
                    assert(std::abs(aligned.start_frame - moving.start_frame) <= per_pixel * 0.50001);
                    for (uint32_t x = 0; x < width; ++x) {
                        same(waveform.detail_column(base, x, width), waveform.detail_column(moving, x, width));
                    }
                }
                for (uint32_t shift = 1; shift <= 4; ++shift) {
                    const WaveformViewport moving{.start_frame = (origin + shift + 0.2) * per_pixel,
                        .span_frames = base.span_frames};
                    for (uint32_t x = 0; x + shift < width; ++x) {
                        same(waveform.detail_column(base, x + shift, width), waveform.detail_column(moving, x, width));
                    }
                }
            }
        }
    }
    assert(WaveformViewport{}.pixel_aligned(800).span_frames == 0);
    assert((WaveformViewport{.span_frames = 100}.pixel_aligned(0).span_frames == 0));
    assert((WaveformViewport{.start_frame = std::numeric_limits<double>::infinity(),
        .span_frames = 100}.pixel_aligned(800).span_frames == 0));
}

void check_time_ruler() {
    for (const uint32_t rate : {44100U, 48000U, 96000U}) {
        for (const uint32_t seconds : {2U, 4U, 8U, 16U, 32U}) {
            const double span = static_cast<double>(seconds) * rate;
            const WaveformViewport centered{.start_frame = span, .span_frames = span};
            const auto ruler = waveform_time_ruler(centered, rate, 100ULL * rate);
            assert(ruler.count == 5);
            for (uint32_t index = 0; index < ruler.count; ++index) {
                assert(near(ruler.ticks[index].frame, span + index * span / 4));
                assert(near(ruler.ticks[index].seconds, seconds + index * seconds / 4.0));
                assert(near(centered.position(ruler.ticks[index].frame), index / 4.0));
            }
            const WaveformViewport start{.start_frame = -span / 2, .span_frames = span};
            const auto head = waveform_time_ruler(start, rate, 100ULL * rate);
            assert(head.count == 3 && head.ticks[0].frame == 0);
            assert(near(start.position(head.ticks[0].frame), 0.5));
            const auto tail = waveform_time_ruler(centered, rate, static_cast<uint64_t>(1.5 * span));
            assert(tail.count == 3 && near(tail.ticks[2].frame, 1.5 * span));
            const WaveformViewport between{.start_frame = span * 1.1, .span_frames = span};
            const auto moving = waveform_time_ruler(between, rate, 100ULL * rate);
            assert(moving.count == 4 && near(moving.ticks[0].frame, span * 1.25));
        }
    }
    assert(waveform_time_ruler({.span_frames = 10}, 0, 100).count == 0);
    assert(waveform_time_ruler({.span_frames = 10}, 48000, 0).count == 0);
    assert(waveform_time_ruler({}, 48000, 100).count == 0);
    assert((waveform_time_ruler({.start_frame = -20, .span_frames = 10}, 48000, 100).count == 0));
    assert((waveform_time_ruler({.start_frame = 101, .span_frames = 10}, 48000, 100).count == 0));
    assert((waveform_time_ruler({.span_frames = std::numeric_limits<double>::quiet_NaN()}, 48000, 100).count == 0));
}

void check_pyramid(const WaveformOverview& waveform) {
    assert(!waveform.detail.empty());
    const auto& base = waveform.detail.front();
    assert(base.peaks.size() <= WaveformOverview::MAX_DETAIL_BINS);
    assert(base.frames_per_bin >= WaveformOverview::MIN_DETAIL_FRAMES);
    assert(waveform.detail.back().peaks.size() == 1 && waveform.detail.size() <= 17);
    std::size_t bins = 0;
    for (std::size_t level = 0; level < waveform.detail.size(); ++level) {
        const auto& current = waveform.detail[level];
        bins += current.peaks.size();
        if (level != 0) {
            assert(current.peaks.size() == (waveform.detail[level - 1].peaks.size() + 1) / 2);
            assert(current.frames_per_bin == waveform.detail[level - 1].frames_per_bin * 2);
        }
        double energy = 0;
        std::array<double, 3> bands{};
        for (std::size_t index = 0; index < current.peaks.size(); ++index) {
            const auto& peak = current.peaks[index];
            const auto frames = std::min<uint64_t>(current.frames_per_bin,
                waveform.media.frame_count - index * current.frames_per_bin);
            assert(std::isfinite(peak.minimum) && std::isfinite(peak.maximum));
            energy += static_cast<double>(peak.rms) * peak.rms * static_cast<double>(frames);
            for (uint32_t band = 0; band < 3; ++band) {
                assert(std::isfinite(peak.band_rms[band]) && peak.band_rms[band] >= 0);
                bands[band] += static_cast<double>(peak.band_rms[band]) * peak.band_rms[band]
                    * static_cast<double>(frames);
            }
        }
        const auto& total = waveform.detail.back().peaks.front();
        assert(near(std::sqrt(energy / static_cast<double>(waveform.media.frame_count)), total.rms));
        for (uint32_t band = 0; band < 3; ++band) {
            assert(near(std::sqrt(bands[band] / static_cast<double>(waveform.media.frame_count)), total.band_rms[band]));
        }
    }
    assert(bins <= 2 * WaveformOverview::MAX_DETAIL_BINS + 17);
}

void write_tones(const std::filesystem::path& path, uint32_t rate, uint64_t frames,
                 const std::array<double, 3>& amplitudes) {
    musicrat::backends::audio::WavWriter writer;
    assert(writer.open(path.c_str(), rate, 2));
    auto block = std::make_unique<AudioBlock>();
    for (uint64_t offset = 0; offset < frames;) {
        *block = {};
        block->sample_rate_hz = rate;
        block->channel_count = 2;
        block->frame_count = static_cast<uint32_t>(std::min<uint64_t>(127, frames - offset));
        for (uint32_t frame = 0; frame < block->frame_count; ++frame) {
            double value = 0;
            const std::array<double, 3> frequencies{60, 1000, 8000};
            for (uint32_t band = 0; band < 3; ++band) {
                value += amplitudes[band] * std::sin(2 * std::numbers::pi
                    * frequencies[band] * static_cast<double>(offset + frame) / rate);
            }
            block->channels[0].push_back(static_cast<AudioBlock::Sample>(value));
            block->channels[1].push_back(static_cast<AudioBlock::Sample>(-value));
        }
        assert(writer.write(*block));
        offset += block->frame_count;
    }
}
} // namespace

int main() {
    check_beat_lines();
    check_scroll_stability();
    check_time_ruler();
    const WaveformPeak blended{.band_rms = {0.8F, 0.4F, 0.2F}};
    assert(waveform_band_color(blended, 0.05F) == 0xffffff);
    assert(waveform_band_color(blended, 0.3F) == 0xffff00);
    assert(waveform_band_color(blended, 0.6F) == 0xff0000);
    assert(waveform_band_color(blended, 0.9F) == 0);
    assert(waveform_rgb(blended) == 0xff8040);
    musicrat::testing::TestDirectory fixture;
    const auto path = fixture.path / "peaks.wav";
    write_track(path, 4103);
    WaveformRequest request{.track_instance_id = 1, .path = path.c_str(),
        .frame_count = 4103, .sample_rate_hz = 48000, .channel_count = 2,
        .expected_asset = regular_file_identity(path.c_str())};
    assert(request.expected_asset);
    WaveformWorker worker;
    worker.submit(request);
    auto result = await(worker, 1);
    assert(result.state == WaveformState::Ready && !result.cache_hit && result.analyzed_frames == 4103);
    const auto first = result.overview;
    assert(first && first->bin_count == 1024 && first->boundary(1024) == 4103);
    assert(first->media.channel_count == 2 && first->media.sample_rate_hz == 48000);
    check_pyramid(*first);
    for (uint32_t bin = 0; bin < first->bin_count; ++bin) {
        double amplitude = 0, energy = 0;
        const auto start = first->boundary(bin);
        const auto end = first->boundary(bin + 1);
        assert(start < end);
        for (auto frame = start; frame < end; ++frame) {
            amplitude = std::max(amplitude, std::abs(sample(frame)));
            energy += sample(frame) * sample(frame);
        }
        // Opposite-polarity channels must not cancel as they would in a mono sum.
        assert(near(first->peaks[bin].minimum, -amplitude));
        assert(near(first->peaks[bin].maximum, amplitude));
        assert(near(first->peaks[bin].rms, std::sqrt(energy / static_cast<double>(end - start))));
    }
    for (const uint32_t width : {800U, 1280U}) {
        bool positive = false, negative = false;
        for (uint32_t x = 0; x < width; ++x) {
            const auto peak = first->column(x, width);
            positive |= peak.maximum > 0.99;
            negative |= peak.minimum < -0.99;
        }
        assert(positive && negative);
    }
    // Every base bin includes all its frames, including a partial final bin.
    for (std::size_t bin = 0; bin < first->detail.front().peaks.size(); ++bin) {
        const auto start = bin * first->detail.front().frames_per_bin;
        const auto end = std::min<uint64_t>(start + first->detail.front().frames_per_bin, 4103);
        double energy = 0, maximum = 0;
        for (auto frame = start; frame < end; ++frame) {
            energy += sample(frame) * sample(frame);
            maximum = std::max(maximum, std::abs(sample(frame)));
        }
        const auto& peak = first->detail.front().peaks[bin];
        assert(near(peak.maximum, maximum) && near(peak.minimum, -maximum));
        assert(near(peak.rms, std::sqrt(energy / static_cast<double>(end - start))));
    }
    assert(first->column(0, 0).maximum == 0 && first->column(800, 800).maximum == 0);
    // Narrow impulses survive any chosen viewport resolution and every pyramid level.
    for (const uint32_t width : {800U, 1280U}) {
        for (const double span : {512.0, 2048.0, 8192.0}) {
            const WaveformViewport viewport{.start_frame = 1000 - span / 2, .span_frames = span};
            assert(near(viewport.position(1000), 0.5));
            bool found = false;
            for (uint32_t x = 0; x < width; ++x) {
                found |= first->detail_column(viewport, x, width).maximum > 0.99;
            }
            assert(found);
        }
    }
    const WaveformViewport before{.start_frame = -100, .span_frames = 100};
    assert(first->detail_column(before, 0, 800).maximum == 0);
    const WaveformViewport after{.start_frame = 4103, .span_frames = 100};
    assert(first->detail_column(after, 0, 800).maximum == 0);
    assert(first->detail_column({.span_frames = 0}, 0, 800).maximum == 0);
    assert(first->detail_column({.span_frames = 100}, 800, 800).maximum == 0);
    assert(first->detail_level(64) == 0 && first->detail_level(128) == 1);
    assert(first->detail_level(1e9) == first->detail.size() - 1);
    WaveformOverview large{};
    large.media.frame_count = std::numeric_limits<uint64_t>::max();
    large.bin_count = 1024;
    assert(large.boundary(1024) == large.media.frame_count);
    for (uint32_t bin = 0; bin < 1024; ++bin) assert(large.boundary(bin) < large.boundary(bin + 1));

    ++request.track_instance_id;
    worker.submit(request);
    result = await(worker, request.track_instance_id);
    assert(result.state == WaveformState::Ready && result.cache_hit && result.overview == first);
    for (int mismatch = 0; mismatch < 3; ++mismatch) {
        auto wrong = request;
        wrong.track_instance_id = 10 + mismatch;
        if (mismatch == 0) ++wrong.frame_count;
        if (mismatch == 1) ++wrong.sample_rate_hz;
        if (mismatch == 2) wrong.channel_count = 1;
        worker.submit(wrong);
        result = await(worker, wrong.track_instance_id);
        assert(result.state == WaveformState::Failed && result.error == WaveformError::MetadataMismatch);
    }
    // Same path and PCM shape but a different asset must not hit the old cache.
    const auto replacement = fixture.path / "replacement.wav";
    write_track(replacement, 4103, 0.5);
    std::filesystem::rename(replacement, path);
    ++request.track_instance_id;
    worker.submit(request);
    result = await(worker, request.track_instance_id);
    assert(result.state == WaveformState::Failed && result.error == WaveformError::AssetChanged);
    request.expected_asset = regular_file_identity(path.c_str());
    worker.submit(request);
    result = await(worker, request.track_instance_id);
    assert(result.state == WaveformState::Ready && !result.cache_hit && result.overview != first);
    assert(result.overview->asset != first->asset);

    const auto tiny = fixture.path / "tiny.wav";
    write_track(tiny, 3);
    request = {.track_instance_id = 20, .path = tiny.c_str(), .frame_count = 3,
        .sample_rate_hz = 48000, .channel_count = 2};
    worker.submit(request);
    result = await(worker, 20);
    assert(result.state == WaveformState::Ready && result.overview->bin_count == 3);
    assert(result.overview->boundary(0) == 0 && result.overview->boundary(3) == 3);
    for (uint32_t bin = 0; bin < 3; ++bin) {
        assert(result.overview->peaks[bin].minimum == 0 && result.overview->peaks[bin].rms == 0);
        assert(waveform_rgb(result.overview->peaks[bin]) == 0x536170);
        assert(waveform_band_color(result.overview->peaks[bin], 0) == 0);
    }
    check_pyramid(*result.overview);
    const auto corrupt = fixture.path / "corrupt.wav";
    musicrat::testing::make_file(corrupt);
    request.track_instance_id = 21;
    request.path = corrupt.c_str();
    worker.submit(request);
    result = await(worker, 21);
    assert(result.state == WaveformState::Failed && result.error == WaveformError::OpenFailed);
    request.track_instance_id = 22;
    request.path = (fixture.path / "missing.wav").c_str();
    worker.submit(request);
    result = await(worker, 22);
    assert(result.state == WaveformState::Failed && result.error == WaveformError::FileUnavailable);
    request.track_instance_id = 23;
    request.frame_count = 0;
    worker.submit(request);
    result = await(worker, 23);
    assert(result.state == WaveformState::Failed && result.error == WaveformError::InvalidRequest);

    // Supersession and unload never publish an older track after the latest request.
    for (uint64_t track = 30; track < 100; ++track) {
        request = {.track_instance_id = track, .path = path.c_str(), .frame_count = 4103,
            .sample_rate_hz = 48000, .channel_count = 2};
        worker.submit(request);
    }
    worker.submit({});
    result = await(worker, 0);
    assert(result.state == WaveformState::Unavailable && !result.overview);
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    assert(!worker.poll(result));

    for (const uint32_t rate : {44100U, 48000U, 96000U}) {
        for (uint32_t band = 0; band < 4; ++band) {
            const auto tones = fixture.path / "tones.wav";
            std::array<double, 3> amplitudes{};
            if (band < 3) amplitudes[band] = 0.6;
            else amplitudes = {0.2, 0.2, 0.2};
            write_tones(tones, rate, rate, amplitudes);
            request = {.track_instance_id = 100 + band, .path = tones.c_str(),
                .frame_count = rate, .sample_rate_hz = rate, .channel_count = 2};
            worker.submit(request);
            result = await(worker, request.track_instance_id);
            if (result.state != WaveformState::Ready) {
                std::fprintf(stderr, "Waveform tone %u Hz band %u: %s decoder:%u\n", rate, band,
                    waveform_error_text(result.error), static_cast<unsigned>(result.decode_error));
            }
            assert(result.state == WaveformState::Ready);
            check_pyramid(*result.overview);
            const auto peak = result.overview->detail_column(
                {.start_frame = rate * 0.25, .span_frames = rate * 0.5}, 0, 1);
            const auto color = waveform_rgb(peak);
            if (band < 3) {
                const auto dominant = peak.band_rms[band];
                assert(dominant > 0.35);
                for (uint32_t other = 0; other < 3; ++other) {
                    if (other != band) assert(dominant > peak.band_rms[other] * 5);
                }
                assert(((color >> ((2 - band) * 8)) & 255) == 255);
                assert(waveform_band_color(peak, dominant * 0.5F) == (0xff0000U >> (band * 8)));
            } else {
                assert((color & 255) > 180 && ((color >> 8) & 255) > 180 && (color >> 16) > 180);
                assert(waveform_band_color(peak, 0.05F) == 0xffffff);
            }
        }
    }
    // Exercise the fixed memory ceiling with a track longer than 65536 * 64 frames.
    const auto long_path = fixture.path / "long.wav";
    write_tones(long_path, 48000, 4200001, {});
    request = {.track_instance_id = 200, .path = long_path.c_str(), .frame_count = 4200001,
        .sample_rate_hz = 48000, .channel_count = 2};
    worker.submit(request);
    result = await(worker, 200);
    assert(result.state == WaveformState::Ready);
    assert(result.overview->detail.front().frames_per_bin == 65);
    check_pyramid(*result.overview);
}
