#include "playback_diagnostics.hpp"
#include "wait_scheduling.hpp"

#include <array>
#include <cassert>

int main() {
    using namespace musicrat::backends::audio;
    detail::PlaybackDiagnosticRecorder recorder;
    AudioOutputDiagnosticEvent event{.timestamp_ns = 1};
    assert(!recorder.select(event));

    std::array<AudioOutputDiagnosticEvent, 2> events{};
    AudioOutputDiagnostics capture{
        .events = events, .slow_operation_threshold_ns = 20'000'000};
    recorder.reset(&capture);
    assert(recorder.enabled());
    assert(recorder.select(event));
    assert(event.reasons == diagnostic_snapshot);
    recorder.append(event);

    event = {.timestamp_ns = 10'000'001};
    assert(!recorder.select(event));
    assert(event.worker_gap_ns == 10'000'000);

    event = {.timestamp_ns = 30'000'001, .write_duration_ns = 20'000'000};
    assert(recorder.select(event));
    assert(event.reasons == (diagnostic_worker_gap | diagnostic_slow_write));
    recorder.append(event);
    assert(capture.count == 2);
    assert(!recorder.has_capacity());
    assert(capture.events[1].write_duration_ns == 20'000'000);

    event = {.timestamp_ns = 31'000'001, .wait_duration_ns = 20'000'000,
             .reasons = diagnostic_wait_error};
    assert(recorder.select(event));
    assert(event.reasons == (diagnostic_slow_wait | diagnostic_wait_error));
    recorder.append(event);
    assert(capture.count == 2 && capture.dropped_events == 1);
    assert(capture.events[1].write_duration_ns == 20'000'000);

    event = {.timestamp_ns = 32'000'001, .correction_ppm = 500};
    assert(recorder.select(event));
    assert(event.reasons == diagnostic_correction_limit);
    event = {.timestamp_ns = 33'000'001, .correction_ppm = 500};
    assert(!recorder.select(event));
    event = {.timestamp_ns = 34'000'001, .correction_ppm = -500};
    assert(recorder.select(event));
    assert(event.reasons == diagnostic_correction_limit);
    event = {.timestamp_ns = 35'000'001, .correction_ppm = 0};
    assert(recorder.select(event));
    assert(event.reasons == diagnostic_correction_limit);
    event = {.timestamp_ns = 36'000'001, .reasons = diagnostic_write_error};
    assert(recorder.select(event));
    assert(event.reasons == diagnostic_write_error);
    event = {.timestamp_ns = 1'000'000'001};
    assert(recorder.select(event));
    assert((event.reasons & diagnostic_snapshot) != 0);

    AudioOutputDiagnostics empty{
        .slow_operation_threshold_ns = 20'000'000};
    recorder.reset(&empty);
    event = {.timestamp_ns = 1};
    assert(recorder.select(event));
    recorder.append(event);
    assert(empty.count == 0 && empty.dropped_events == 1);
    recorder.reset(nullptr);
    assert(!recorder.enabled());
    assert(!recorder.select(event));

    detail::ThreadSchedulingCounters counters{};
    assert(detail::parse_thread_scheduling("123 456 7\n", counters));
    assert(counters.runtime_ns == 123 && counters.runnable_delay_ns == 456
        && counters.timeslices == 7);
    for (const auto text : {"", "1 2", "-1 2 3", "1x 2 3", "1 2 3 4",
                           "18446744073709551616 2 3", "1 2 x"}) {
        assert(!detail::parse_thread_scheduling(text, counters));
        assert(counters.runtime_ns == 123);
    }
    assert(detail::parse_thread_scheduling(" \t0  0\t0\n", counters));
    detail::WaitSchedulingObserver observer;
    assert(observer.open() == 0);
    assert(observer.read(counters) == 0);
}
