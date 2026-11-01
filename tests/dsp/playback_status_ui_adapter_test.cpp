#include <musicrat/dsp/playback_status_ui_adapter.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <new>

namespace {
std::atomic<bool> track_allocations{false};
std::atomic<uint64_t> allocations{0};
}

void* operator new(std::size_t size) {
    if (track_allocations.load(std::memory_order_relaxed)) ++allocations;
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    using namespace CommRaT::Messages;
    using namespace CommRaT::Parameters;
    using Error = musicrat::dsp::PlaybackStatusUiAdapterConfigError;
    musicrat::dsp::PlaybackStatusUiAdapter adapter{};
    const std::array bindings{
        CompiledPlaybackObservationBinding{
            .binding_id = 1, .surface_id = "deck", .widget_id = "elapsed",
            .property = WIDGET_PROPERTY_TEXT, .selector = PLAYBACK_OBSERVE_ELAPSED_TIME},
        CompiledPlaybackObservationBinding{
            .binding_id = 2, .surface_id = "deck", .widget_id = "remaining",
            .property = WIDGET_PROPERTY_TEXT, .selector = PLAYBACK_OBSERVE_REMAINING_TIME},
        CompiledPlaybackObservationBinding{
            .binding_id = 3, .surface_id = "deck", .widget_id = "position",
            .selector = PLAYBACK_OBSERVE_POSITION},
        CompiledPlaybackObservationBinding{
            .binding_id = 4, .surface_id = "deck", .widget_id = "playing",
            .property = WIDGET_PROPERTY_ACTIVE, .selector = PLAYBACK_OBSERVE_PLAYING},
    };
    assert(adapter.configure(bindings) == Error::None);
    PlaybackStatusBlock input{};
    input.timestamp_ns = 1234;
    input.sequence_number = 9;
    input.source_sample_rate_hz = 48000;
    input.media_position_frames = 48000 * 61.0 + 12000;
    input.duration_frames = 48000 * 120;
    input.playback_rate = 2.0;
    input.flags = PLAYBACK_STATUS_READY | PLAYBACK_STATUS_PLAYING;
    WidgetUpdateBlock output{};
    track_allocations = true;
    const auto result = adapter.process(input, output);
    track_allocations = false;
    assert(allocations == 0);
    assert(result.mapped_count == 4 && result.invalid_count == 0);
    assert(output.flags == 0 && output.events.size() == 4);
    assert(output.timestamp_ns == 1234 && output.sequence_number == 9);
    assert(output.events[0].text == "01:01.250");
    assert(output.events[1].text == "00:58.750");
    assert(output.events[2].value == 61.25 / 120.0);
    assert(output.events[3].kind == WIDGET_VALUE_BOOLEAN && output.events[3].active);

    input.source_sample_rate_hz = 44100;
    input.media_position_frames = 44100 * 3601.0;
    input.duration_frames = 44100 * 3602ULL;
    input.playback_rate = 0.5;
    input.flags = PLAYBACK_STATUS_READY;
    (void)adapter.process(input, output);
    assert(output.events[0].text == "60:01.000");
    assert(output.events[1].text == "00:01.000");
    assert(!output.events[3].active);

    input.media_position_frames = static_cast<double>(input.duration_frames) + 10;
    (void)adapter.process(input, output);
    assert(output.events[1].text == "00:00.000");
    assert(output.events[2].value == 1.0);

    input.duration_frames = 0;
    assert(adapter.process(input, output).unavailable_count == 2);
    assert(output.events[1].text == "--:--.---");
    assert(output.events.size() == 4);
    assert(output.events[1].kind == WIDGET_VALUE_UNAVAILABLE);
    assert(output.events[2].kind == WIDGET_VALUE_UNAVAILABLE);
    assert(output.flags == 0);
    input.source_sample_rate_hz = 0;
    (void)adapter.process(input, output);
    assert(output.events[0].text == "--:--.---");

    input.flags = PLAYBACK_STATUS_INVALID;
    assert(adapter.process(input, output).invalid_count == 4);
    assert(output.events.size() == 4);
    assert(output.events[0].text == "--:--.---");

    input = {};
    input.source_sample_rate_hz = 48000;
    input.duration_frames = 96000;
    input.media_position_frames = 24000;
    ++input.generation;
    (void)adapter.process(input, output);
    assert(output.events[0].text == "00:00.500");
    assert(output.events[2].value == 0.25);
    input.media_position_frames = std::numeric_limits<double>::quiet_NaN();
    assert(adapter.process(input, output).invalid_count >= 1);
    assert(output.events[2].kind == WIDGET_VALUE_UNAVAILABLE);

    auto invalid = bindings;
    invalid[1].binding_id = 1;
    assert(adapter.configure(invalid) == Error::DuplicateBindingId);
    invalid = bindings;
    invalid[0].property = WIDGET_PROPERTY_VALUE;
    assert(adapter.configure(invalid) == Error::InvalidBinding);
    invalid = bindings;
    invalid[2].minimum = invalid[2].maximum;
    assert(adapter.configure(invalid) == Error::InvalidBinding);
    invalid[2].minimum = -std::numeric_limits<double>::max();
    invalid[2].maximum = std::numeric_limits<double>::max();
    assert(adapter.configure(invalid) == Error::InvalidBinding);
    assert(adapter.configure(nullptr, 1) == Error::InvalidBinding);
    assert(adapter.configure(nullptr, adapter.MAX_BINDINGS + 1) == Error::TooManyBindings);

    std::array<CompiledPlaybackObservationBinding, adapter.MAX_BINDINGS> full{};
    for (std::size_t index = 0; index < full.size(); ++index) {
        full[index] = bindings[0];
        full[index].binding_id = static_cast<uint32_t>(index + 1);
    }
    assert(adapter.configure(full) == Error::None);
    input.media_position_frames = std::numeric_limits<double>::max();
    (void)adapter.process(input, output);
    assert(output.events.size() == full.size());
    assert(output.events[0].text == "--:--.---");

    const std::array numeric{
        CompiledPlaybackObservationBinding{
            .binding_id = 1, .surface_id = "deck", .widget_id = "rate",
            .selector = PLAYBACK_OBSERVE_RATE, .minimum = 0.5, .maximum = 2.0},
        CompiledPlaybackObservationBinding{
            .binding_id = 2, .surface_id = "deck", .widget_id = "state",
            .selector = PLAYBACK_OBSERVE_WORKER_STATE},
        CompiledPlaybackObservationBinding{
            .binding_id = 3, .surface_id = "deck", .widget_id = "error",
            .selector = PLAYBACK_OBSERVE_DECODE_ERROR},
    };
    assert(adapter.configure(numeric) == Error::None);
    input.playback_rate = 1.25;
    input.worker_state = PLAYBACK_WORKER_READY;
    input.decode_error = PLAYBACK_DECODE_ERROR_FILE_IO;
    (void)adapter.process(input, output);
    assert(output.events[0].value == 0.5);
    assert(output.events[1].kind == WIDGET_VALUE_CHOICE);
    assert(output.events[1].value == PLAYBACK_WORKER_READY);
    assert(output.events[2].value == PLAYBACK_DECODE_ERROR_FILE_IO);
    input.worker_state = 255;
    assert(adapter.process(input, output).invalid_count == 1);
    assert(output.events.size() == 3);
    assert(output.events[1].kind == WIDGET_VALUE_UNAVAILABLE);

    const std::array load_bindings{
        CompiledPlaybackObservationBinding{
            .binding_id = 1, .surface_id = "deck", .widget_id = "load-state",
            .selector = PLAYBACK_OBSERVE_LOAD_STATE},
        CompiledPlaybackObservationBinding{
            .binding_id = 2, .surface_id = "deck", .widget_id = "load-error",
            .selector = PLAYBACK_OBSERVE_LOAD_DECODE_ERROR},
    };
    assert(adapter.configure(load_bindings) == Error::None);
    input.flags = PLAYBACK_STATUS_INVALID;
    input.load_state = MEDIA_LOAD_FAILED;
    input.load_decode_error = PLAYBACK_DECODE_ERROR_FILE_OPEN;
    assert(adapter.process(input, output).invalid_count == 0);
    assert(output.events[0].kind == WIDGET_VALUE_CHOICE);
    assert(output.events[0].value == MEDIA_LOAD_FAILED);
    assert(output.events[1].value == PLAYBACK_DECODE_ERROR_FILE_OPEN);
    input.load_state = 255;
    assert(adapter.process(input, output).invalid_count == 1);
    assert(output.events[0].kind == WIDGET_VALUE_UNAVAILABLE);
}
