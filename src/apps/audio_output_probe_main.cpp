#include <musicrat/backends/audio/audio_output_backend.hpp>
#include <musicrat/config.hpp>

#include <corerat/platform/threading.hpp>
#include <corerat/platform/timestamp.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <string_view>
#include <vector>

namespace {

constexpr uint64_t telemetry_interval_ns = 1'000'000'000ULL;
constexpr uint64_t maximum_probe_duration_seconds = 24 * 60 * 60;
constexpr std::size_t backend_diagnostic_event_reserve = 32'768;

struct TelemetrySample {
    uint64_t elapsed_ns{0};
    uint64_t block_sequence{0};
    uint64_t queued_frames{0};
    int32_t rate_correction_ppm{0};
    uint64_t rejected_blocks{0};
    uint64_t underruns{0};
    uint64_t missing_frames{0};
    uint64_t stream_failures{0};
};

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
    int32_t clock_offset_ppm{0};
    uint32_t backend_events{0};
    uint32_t wait_scheduling{0};
};

long double estimated_duration_seconds(
    uint64_t total_frames, const Options& options) {
    const long double producer_rate_scale =
        1'000'000.0L + options.clock_offset_ppm;
    const long double nominal_seconds =
        static_cast<long double>(total_frames) / options.rate;
    const uint64_t jitter_count =
        (static_cast<uint64_t>(options.blocks) + options.jitter_every - 1)
        / options.jitter_every;
    return nominal_seconds * 1'000'000.0L / producer_rate_scale
        + static_cast<long double>(jitter_count) * options.jitter_ms / 1000.0L;
}

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
        else if (key == "--backend-events") target = &options.backend_events;
        else if (key == "--wait-scheduling") target = &options.wait_scheduling;
        else if (key == "--clock-offset-ppm") {
            const auto result = std::from_chars(
                value.data(), value.data() + value.size(),
                options.clock_offset_ppm);
            if (result.ec != std::errc{}
                || result.ptr != value.data() + value.size()) {
                std::fprintf(stderr, "Invalid signed integer for %.*s\n",
                    static_cast<int>(key.size()), key.data());
                return false;
            }
            continue;
        }
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
        || options.jitter_ms > 1000
        || options.clock_offset_ppm < -1000
        || options.clock_offset_ppm > 1000 || options.backend_events > 1
        || options.wait_scheduling > 1
        || (options.wait_scheduling && !options.backend_events)) {
        std::fprintf(stderr, "Invalid format, block count, or jitter (maximum 1000 ms)\n");
        return false;
    }
    const uint64_t total_frames =
        static_cast<uint64_t>(options.blocks) * options.frames;
    if (estimated_duration_seconds(total_frames, options)
        > maximum_probe_duration_seconds) {
        std::fprintf(stderr, "Probe duration exceeds the 24-hour telemetry bound\n");
        return false;
    }
    return true;
}

int run(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        return 2;
    }
    const uint64_t total_frames =
        static_cast<uint64_t>(options.blocks) * options.frames;
    const std::size_t telemetry_capacity = static_cast<std::size_t>(
        std::ceil(estimated_duration_seconds(total_frames, options)) + 2);
    std::vector<TelemetrySample> telemetry_samples(telemetry_capacity);
    std::vector<musicrat::backends::audio::AudioOutputDiagnosticEvent>
        diagnostic_events(options.backend_events
                ? telemetry_capacity + backend_diagnostic_event_reserve
                : 0);
    musicrat::backends::audio::AudioOutputDiagnostics diagnostics{
        .events = diagnostic_events,
        .wait_scheduling = options.wait_scheduling != 0};
    auto backend = musicrat::backends::audio::make_audio_output_backend();
    if (options.backend_events && std::string_view{backend->name()} != "alsa") {
        std::fprintf(stderr, "Backend event capture requires the ALSA backend\n");
        return 2;
    }
    if (!backend->open({
            .device_name = options.device,
            .sample_rate_hz = options.rate,
            .channel_count = static_cast<uint16_t>(options.channels),
            .period_frames = options.frames,
            .diagnostics = options.backend_events ? &diagnostics : nullptr,
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
    uint64_t producer_start_timestamp_ns = 0;
    std::size_t telemetry_count = 0;
    bool telemetry_overflow = false;
    bool attached = true;
    corerat::Thread producer{[&] {
#if defined(CORERAT_PLATFORM_EVL)
        attached = evl_get_self() >= 0;
        if (!attached) {
            return;
        }
#endif
        const auto start = corerat::Time::now();
        producer_start_timestamp_ns = start;
        const uint64_t nominal_period_ns =
            (static_cast<uint64_t>(options.frames) * 1'000'000'000ULL + options.rate / 2)
            / options.rate;
        const uint64_t rate_scale = static_cast<uint64_t>(
            static_cast<int64_t>(1'000'000) + options.clock_offset_ppm);
        const uint64_t period_ns =
            (nominal_period_ns * 1'000'000ULL + rate_scale / 2) / rate_scale;
        double phase = 0;
        const uint64_t fade_frames =
            static_cast<uint64_t>(options.rate) * 20 / 1000;
        uint64_t next_telemetry_elapsed_ns = 0;
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
                const uint64_t stream_frame =
                    static_cast<uint64_t>(index) * options.frames + frame;
                const uint64_t frames_to_end =
                    total_frames - stream_frame - 1;
                const double envelope = std::min(
                    1.0,
                    static_cast<double>(std::min(stream_frame, frames_to_end))
                        / std::max<uint64_t>(fade_frames, 1));
                const auto sample = static_cast<musicrat::config::sample_type>(
                    0.05 * envelope
                    * std::sin(phase * 6.283185307179586));
                phase += 440.0 / options.rate;
                phase -= std::floor(phase);
                for (uint32_t channel = 0; channel < options.channels; ++channel) {
                    block.channels[channel][frame] = sample;
                }
            }
            rejected += !backend->write(block);
            const uint64_t elapsed = corerat::Time::now() - start;
            if (elapsed >= next_telemetry_elapsed_ns) {
                if (telemetry_count >= telemetry_samples.size()) {
                    telemetry_overflow = true;
                    continue;
                }
                telemetry_samples[telemetry_count++] = {
                    .elapsed_ns = elapsed,
                    .block_sequence = index,
                    .queued_frames = backend->queued_frames(),
                    .rate_correction_ppm = backend->rate_correction_ppm(),
                    .rejected_blocks = rejected,
                    .underruns = backend->underrun_count(),
                    .missing_frames = backend->underrun_frame_count(),
                    .stream_failures = backend->stream_failure_count(),
                };
                next_telemetry_elapsed_ns =
                    (elapsed / telemetry_interval_ns + 1)
                    * telemetry_interval_ns;
            }
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
    const auto queued_frames = backend->queued_frames();
    const auto queue_high_water_frames = backend->queue_high_water_frames();
    const auto queue_capacity_frames = backend->queue_capacity();
    const auto rate_correction_ppm = backend->rate_correction_ppm();
    const auto maximum_rate_correction_ppm =
        backend->maximum_rate_correction_ppm();
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
        "\"jitter_ms\":%u,\"jitter_every\":%u,\"clock_offset_ppm\":%d,"
        "\"producer_start_timestamp_ns\":%llu,"
        "\"elapsed_ns\":%llu,"
        "\"maximum_producer_lateness_ns\":%llu,\"rejected_blocks\":%llu,"
        "\"underruns\":%llu,\"missing_frames\":%llu,\"stream_failures\":%llu,"
        "\"queued_frames_at_end\":%llu,\"queue_high_water_frames\":%llu,"
        "\"queue_capacity_frames\":%llu,\"current_rate_correction_ppm\":%d,"
        "\"maximum_rate_correction_ppm\":%u,\"telemetry_overflow\":%s,"
        "\"telemetry_interval_ns\":%llu,"
        "\"telemetry_samples\":[",
        backend->name(), platform, options.rate, options.channels, options.frames,
        options.blocks, options.jitter_ms, options.jitter_every,
        static_cast<int>(options.clock_offset_ppm),
        static_cast<unsigned long long>(producer_start_timestamp_ns),
        static_cast<unsigned long long>(elapsed_ns),
        static_cast<unsigned long long>(maximum_lateness_ns),
        static_cast<unsigned long long>(rejected),
        static_cast<unsigned long long>(underruns),
        static_cast<unsigned long long>(missing_frames),
        static_cast<unsigned long long>(failures),
        static_cast<unsigned long long>(queued_frames),
        static_cast<unsigned long long>(queue_high_water_frames),
        static_cast<unsigned long long>(queue_capacity_frames),
        static_cast<int>(rate_correction_ppm),
        static_cast<unsigned>(maximum_rate_correction_ppm),
        telemetry_overflow ? "true" : "false",
        static_cast<unsigned long long>(telemetry_interval_ns));
    for (std::size_t index = 0; index < telemetry_count; ++index) {
        const auto& sample = telemetry_samples[index];
        std::printf(
            "%s{\"elapsed_ns\":%llu,\"block_sequence\":%llu,"
            "\"queued_frames\":%llu,\"rate_correction_ppm\":%d,"
            "\"rejected_blocks\":%llu,\"underruns\":%llu,"
            "\"missing_frames\":%llu,\"stream_failures\":%llu}",
            index == 0 ? "" : ",",
            static_cast<unsigned long long>(sample.elapsed_ns),
            static_cast<unsigned long long>(sample.block_sequence),
            static_cast<unsigned long long>(sample.queued_frames),
            static_cast<int>(sample.rate_correction_ppm),
            static_cast<unsigned long long>(sample.rejected_blocks),
            static_cast<unsigned long long>(sample.underruns),
            static_cast<unsigned long long>(sample.missing_frames),
            static_cast<unsigned long long>(sample.stream_failures));
    }
    std::printf("],\"backend_diagnostics\":");
    if (!options.backend_events) {
        std::printf("null");
    } else {
        std::printf(
            "{\"start_timestamp_ns\":%llu,\"slow_operation_threshold_ns\":%llu,"
            "\"device_period_frames\":%llu,\"device_buffer_frames\":%llu,"
            "\"device_sample_rate_hz\":%u,\"capacity\":%zu,"
            "\"dropped_events\":%llu,\"status_failures\":%llu,"
            "\"wait_scheduling\":%s,\"scheduling_failures\":%llu,\"events\":[",
            static_cast<unsigned long long>(diagnostics.start_timestamp_ns),
            static_cast<unsigned long long>(diagnostics.slow_operation_threshold_ns),
            static_cast<unsigned long long>(diagnostics.device_period_frames),
            static_cast<unsigned long long>(diagnostics.device_buffer_frames),
            diagnostics.device_sample_rate_hz, diagnostic_events.size(),
            static_cast<unsigned long long>(diagnostics.dropped_events),
            static_cast<unsigned long long>(diagnostics.status_failures),
            diagnostics.wait_scheduling ? "true" : "false",
            static_cast<unsigned long long>(diagnostics.scheduling_failures));
        for (std::size_t index = 0; index < diagnostics.count; ++index) {
            const auto& event = diagnostic_events[index];
            std::printf(
                "%s{\"timestamp_ns\":%llu,\"observation_timestamp_ns\":%llu,"
                "\"worker_gap_ns\":%llu,\"write_duration_ns\":%llu,"
                "\"wait_duration_ns\":%llu,\"wait_start_timestamp_ns\":%llu,"
                "\"scheduling_before_timestamp_ns\":%llu,"
                "\"scheduling_after_timestamp_ns\":%llu,"
                "\"scheduling_runtime_ns\":%llu,"
                "\"scheduling_runnable_delay_ns\":%llu,"
                "\"scheduling_timeslices\":%llu,\"scheduling_result\":%d,"
                "\"queued_before\":%llu,"
                "\"queued_after\":%llu,\"source_frames_consumed\":%llu,"
                "\"device_frames_written\":%llu,\"write_result\":%lld,"
                "\"device_available_frames\":%lld,\"device_delay_frames\":%lld,"
                "\"requested_frames\":%u,\"reasons\":%u,\"correction_ppm\":%d,"
                "\"wait_result\":%d,\"recovery_result\":%d,\"device_state\":%d,"
                "\"device_status_result\":%d}",
                index == 0 ? "" : ",",
                static_cast<unsigned long long>(event.timestamp_ns),
                static_cast<unsigned long long>(event.observation_timestamp_ns),
                static_cast<unsigned long long>(event.worker_gap_ns),
                static_cast<unsigned long long>(event.write_duration_ns),
                static_cast<unsigned long long>(event.wait_duration_ns),
                static_cast<unsigned long long>(event.wait_start_timestamp_ns),
                static_cast<unsigned long long>(event.scheduling_before_timestamp_ns),
                static_cast<unsigned long long>(event.scheduling_after_timestamp_ns),
                static_cast<unsigned long long>(event.scheduling_runtime_ns),
                static_cast<unsigned long long>(event.scheduling_runnable_delay_ns),
                static_cast<unsigned long long>(event.scheduling_timeslices),
                event.scheduling_result,
                static_cast<unsigned long long>(event.queued_before),
                static_cast<unsigned long long>(event.queued_after),
                static_cast<unsigned long long>(event.source_frames_consumed),
                static_cast<unsigned long long>(event.device_frames_written),
                static_cast<long long>(event.write_result),
                static_cast<long long>(event.device_available_frames),
                static_cast<long long>(event.device_delay_frames),
                event.requested_frames, event.reasons, event.correction_ppm,
                event.wait_result, event.recovery_result, event.device_state,
                event.device_status_result);
        }
        std::printf("]}");
    }
    std::puts("}");
    return rejected == 0 && underruns == 0 && missing_frames == 0 && failures == 0
            && queued_frames <= queue_capacity_frames
            && queue_high_water_frames <= queue_capacity_frames
            && !telemetry_overflow
            && diagnostics.dropped_events == 0
            && diagnostics.scheduling_failures == 0
        ? 0
        : 1;
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
