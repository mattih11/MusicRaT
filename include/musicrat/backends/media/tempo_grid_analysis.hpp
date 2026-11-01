#pragma once

#include <musicrat/backends/media/media_decoder.hpp>
#include <musicrat/utility/spsc_queue.hpp>

#include <sertial/containers/fixed_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <new>
#include <numeric>
#include <stop_token>
#include <thread>
#include <vector>

namespace musicrat::backends::media {

struct TempoGridAnalysisResult {
    uint64_t request_id{0};
    uint32_t sample_rate_hz{0};
    double tempo_bpm{0};
    uint16_t confidence_per_mille{0};
    bool valid{false};
};

class TempoGridAnalysisWorker {
public:
    TempoGridAnalysisWorker() {
        thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
    }
    ~TempoGridAnalysisWorker() {
        thread_.request_stop();
        wake_.notify_one();
        thread_.join();
    }

    [[nodiscard]] bool submit(uint64_t request_id, const char* path) noexcept {
        if (request_id == 0 || path == nullptr || *path == '\0') return false;
        std::lock_guard lock(mutex_);
        pending_ = {};
        pending_.request_id = request_id;
        pending_.path = path;
        pending_ready_ = true;
        wake_.notify_one();
        return true;
    }
    [[nodiscard]] bool poll(TempoGridAnalysisResult& result) noexcept {
        return results_.pop(result);
    }

private:
    struct Request {
        uint64_t request_id{0};
        sertial::fixed_string<512> path{};
    };

    static void analyze(const Request& request, TempoGridAnalysisResult& result) noexcept {
        constexpr uint32_t analysis_hop = 512;
        constexpr uint32_t max_seconds = 120;
        constexpr double min_bpm = 70.0;
        constexpr double max_bpm = 180.0;
        constexpr uint16_t minimum_confidence = 120;
        const auto lag_bpm = [](uint32_t sample_rate_hz, uint32_t lag) noexcept {
            return 60.0 * sample_rate_hz / (analysis_hop * lag);
        };
        const auto lag_score = [](const std::vector<double>& novelty, uint32_t lag) noexcept {
            double score = 0;
            for (std::size_t index = lag; index < novelty.size(); ++index) {
                score += novelty[index] * novelty[index - lag];
            }
            return score;
        };
        MediaDecoder decoder{};
        if (!decoder.open(request.path.c_str())) return;
        const auto metadata = decoder.metadata();
        if (metadata.sample_rate_hz == 0 || metadata.channel_count == 0) return;
        const uint64_t max_frames = metadata.frame_count == 0
            ? uint64_t{metadata.sample_rate_hz} * max_seconds
            : std::min(metadata.frame_count, uint64_t{metadata.sample_rate_hz} * max_seconds);
        std::vector<double> novelty{};
        try {
            novelty.reserve(max_frames / analysis_hop + 8);
        } catch (const std::bad_alloc&) {
            return;
        }
        CommRaT::Messages::AudioBlock block{};
        uint64_t processed = 0;
        double accumulator = 0;
        uint32_t accumulator_frames = 0;
        double previous_energy = 0;
        while (processed < max_frames) {
            const auto request_frames = static_cast<uint32_t>(std::min<uint64_t>(2048, max_frames - processed));
            const auto state = decoder.read(block, request_frames);
            if (state == DecodeResult::Error) return;
            if (block.frame_count == 0) break;
            for (uint32_t frame = 0; frame < block.frame_count; ++frame) {
                double mono = 0;
                for (uint16_t channel = 0; channel < block.channel_count; ++channel) {
                    mono += std::abs(block.channels[channel][frame]);
                }
                accumulator += mono / block.channel_count;
                ++accumulator_frames;
                if (accumulator_frames == analysis_hop) {
                    const double energy = accumulator / analysis_hop;
                    novelty.push_back(std::max(0.0, energy - previous_energy));
                    previous_energy = energy;
                    accumulator = 0;
                    accumulator_frames = 0;
                }
            }
            processed += block.frame_count;
            if (state == DecodeResult::EndOfStream) break;
        }
        if (novelty.size() < 64) return;
        const double mean = std::accumulate(novelty.begin(), novelty.end(), 0.0) / novelty.size();
        for (auto& value : novelty) {
            value = std::max(0.0, value - mean);
        }
        const auto min_lag = std::max<uint32_t>(
            1, static_cast<uint32_t>(std::floor(lag_bpm(metadata.sample_rate_hz, 1) / max_bpm)));
        const auto max_lag = static_cast<uint32_t>(std::ceil(lag_bpm(metadata.sample_rate_hz, 1) / min_bpm));
        if (max_lag + 1 >= novelty.size() || min_lag >= max_lag) return;
        double best = 0;
        uint32_t best_lag = 0;
        for (uint32_t lag = min_lag; lag <= max_lag; ++lag) {
            const auto score = lag_score(novelty, lag);
            if (score > best) {
                best = score;
                best_lag = lag;
            }
        }
        if (best_lag == 0 || !std::isfinite(best) || best <= 0) return;
        for (uint32_t candidate : {best_lag / 2, best_lag * 2}) {
            if (candidate < min_lag || candidate > max_lag) continue;
            const auto score = lag_score(novelty, candidate);
            if (score > best * 0.94) {
                best = score;
                best_lag = candidate;
            }
        }
        const auto bpm = lag_bpm(metadata.sample_rate_hz, best_lag);
        if (!std::isfinite(bpm) || bpm < min_bpm || bpm > max_bpm) return;
        const double energy = std::accumulate(novelty.begin(), novelty.end(), 0.0);
        if (!std::isfinite(energy) || energy <= std::numeric_limits<double>::epsilon()) return;
        result.sample_rate_hz = metadata.sample_rate_hz;
        result.tempo_bpm = bpm;
        result.confidence_per_mille = static_cast<uint16_t>(std::clamp(best * 3000.0 / energy, 0.0, 1000.0));
        if (result.confidence_per_mille < minimum_confidence) return;
        result.valid = true;
    }
    void run(std::stop_token stop) noexcept {
        while (!stop.stop_requested()) {
            Request request{};
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stop.stop_requested() || pending_ready_; });
                if (stop.stop_requested()) break;
                request = pending_;
                pending_ready_ = false;
            }
            TempoGridAnalysisResult result{.request_id = request.request_id};
            analyze(request, result);
            while (!results_.push(result) && !stop.stop_requested()) {
                TempoGridAnalysisResult discarded{};
                static_cast<void>(results_.pop(discarded));
            }
        }
    }

    std::mutex mutex_{};
    std::condition_variable_any wake_{};
    Request pending_{};
    bool pending_ready_{false};
    SpscQueue<TempoGridAnalysisResult, 8> results_{};
    std::jthread thread_{};
};

} // namespace musicrat::backends::media
