#include <musicrat/backends/audio/audio_output_backend.hpp>
#include <musicrat/config.hpp>

#include <corerat/platform/threading.hpp>
#include <corerat/platform/timestamp.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <string_view>

namespace {

struct Options {
    const char* device{""};
    uint32_t rate{static_cast<uint32_t>(musicrat::config::default_sample_rate_hz)};
    uint32_t frames{static_cast<uint32_t>(std::llround(
        musicrat::config::default_sample_rate_hz
        * musicrat::config::default_block_period_ms / 1000.0))};
    uint32_t channels{1};
    uint32_t blocks{3000};
    uint32_t jitter_ms{0};
    uint32_t jitter_every{37};
};

bool parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view key{argv[i]};
        if (i + 1 == argc) {
            std::fprintf(stderr, "Missing value for %s\n", argv[i]);
            return false;
        }
        const std::string_view value{argv[++i]};
        if (key == "--device") {
            options.device = argv[i];
            continue;
        }
        uint32_t* target = nullptr;
        if (key == "--sample-rate") target = &options.rate;
        else if (key == "--period-frames") target = &options.frames;
        else if (key == "--channels") target = &options.channels;
        else if (key == "--blocks") target = &options.blocks;
        else if (key == "--jitter-ms") target = &options.jitter_ms;
        else if (key == "--jitter-every") target = &options.jitter_every;
        else {
            std::fprintf(stderr, "Unknown option: %.*s\n", static_cast<int>(key.size()), key.data());
            return false;
        }
        const auto result = std::from_chars(value.data(), value.data() + value.size(), *target);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
            std::fprintf(stderr, "Invalid unsigned integer for %.*s\n",
                static_cast<int>(key.size()), key.data());
            return false;
        }
    }
    if (options.rate < 8000 || options.rate > 192000 || options.frames == 0
        || options.frames > musicrat::config::max_audio_frames
        || options.channels == 0
        || options.channels > musicrat::config::max_audio_channels
        || options.channels > std::numeric_limits<uint16_t>::max()
        || options.blocks == 0 || options.jitter_every == 0
        || options.jitter_ms > 1000) {
        std::fprintf(stderr, "Invalid format, block count, or jitter (maximum 1000 ms)\n");
        return false;
    }
    return true;
}

int run(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        return 2;
    }
    auto backend = musicrat::backends::audio::make_audio_output_backend();
    if (!backend->open({
            .device_name = options.device,
            .sample_rate_hz = options.rate,
            .channel_count = static_cast<uint16_t>(options.channels),
            .period_frames = options.frames,
        })) {
        std::fprintf(stderr, "Failed to open %s output device\n", backend->name());
        return 1;
    }
    CommRaT::Messages::AudioBlock block{};
    block.sample_rate_hz = options.rate;
    block.channel_count = static_cast<uint16_t>(options.channels);
    block.frame_count = options.frames;
    for (uint32_t channel = 0; channel < options.channels; ++channel) {
        for (uint32_t frame = 0; frame < options.frames; ++frame) {
            block.channels[channel].push_back(0);
        }
    }

    uint64_t rejected = 0;
    uint64_t elapsed_ns = 0;
    uint64_t maximum_lateness_ns = 0;
    bool attached = true;
    corerat::Thread producer{[&] {
#if defined(CORERAT_PLATFORM_EVL)
        attached = evl_get_self() >= 0;
        if (!attached) {
            return;
        }
#endif
        const auto start = corerat::Time::now();
        const uint64_t period_ns =
            (static_cast<uint64_t>(options.frames) * 1'000'000'000ULL + options.rate / 2)
            / options.rate;
        double phase = 0;
        for (uint32_t index = 0; index < options.blocks; ++index) {
            const auto deadline = start + static_cast<uint64_t>(index) * period_ns;
            corerat::Time::sleep_until(deadline);
            if (options.jitter_ms != 0 && index % options.jitter_every == 0) {
                corerat::Time::sleep(corerat::Milliseconds(options.jitter_ms));
            }
            const auto now = corerat::Time::now();
            if (now > deadline && now - deadline > maximum_lateness_ns) {
                maximum_lateness_ns = now - deadline;
            }
            block.timestamp_ns = now;
            block.sequence_number = index;
            for (uint32_t frame = 0; frame < options.frames; ++frame) {
                const auto sample = static_cast<musicrat::config::sample_type>(
                    0.05 * std::sin(phase * 6.283185307179586));
                phase += 440.0 / options.rate;
                phase -= std::floor(phase);
                for (uint32_t channel = 0; channel < options.channels; ++channel) {
                    block.channels[channel][frame] = sample;
                }
            }
            rejected += !backend->write(block);
        }
        elapsed_ns = corerat::Time::now() - start;
    }};
    if (!producer.joinable()) {
        std::fprintf(stderr, "Failed to start platform producer thread\n");
        backend->close();
        return 1;
    }
    producer.join();
    const auto underruns = backend->underrun_count();
    const auto missing_frames = backend->underrun_frame_count();
    const auto failures = backend->stream_failure_count();
    backend->close();
    if (!attached) {
        std::fprintf(stderr, "Producer thread failed EVL attachment\n");
        return 1;
    }
#if defined(CORERAT_PLATFORM_EVL)
    constexpr const char* platform = "evl";
#else
    constexpr const char* platform = "std";
#endif
    std::printf(
        "{\"backend\":\"%s\",\"platform\":\"%s\",\"sample_rate_hz\":%u,"
        "\"channels\":%u,\"period_frames\":%u,\"blocks\":%u,"
        "\"jitter_ms\":%u,\"jitter_every\":%u,\"elapsed_ns\":%llu,"
        "\"maximum_producer_lateness_ns\":%llu,\"rejected_blocks\":%llu,"
        "\"underruns\":%llu,\"missing_frames\":%llu,\"stream_failures\":%llu}\n",
        backend->name(), platform, options.rate, options.channels, options.frames,
        options.blocks, options.jitter_ms, options.jitter_every,
        static_cast<unsigned long long>(elapsed_ns),
        static_cast<unsigned long long>(maximum_lateness_ns),
        static_cast<unsigned long long>(rejected),
        static_cast<unsigned long long>(underruns),
        static_cast<unsigned long long>(missing_frames),
        static_cast<unsigned long long>(failures));
    return rejected == 0 && underruns == 0 && missing_frames == 0 && failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Playback probe failed: %s\n", error.what());
        return 1;
    }
}
