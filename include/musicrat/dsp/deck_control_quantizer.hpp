#pragma once

#include <musicrat/protocol/deck_control.hpp>
#include <musicrat/protocol/transport.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace musicrat::dsp {

struct DeckControlQuantizerResult {
    uint32_t pending_count{0};
    uint32_t dropped_count{0};
};

class DeckControlQuantizer {
public:
    using Event = CommRaT::Messages::DeckControlEvent;
    using EventBlock = CommRaT::Messages::DeckControlEventBlock;
    using TransportBlock = CommRaT::Messages::TransportBlock;

    DeckControlQuantizerResult process(
        const EventBlock* input,
        const TransportBlock* transport,
        uint32_t frame_count,
        EventBlock& output) noexcept {
        output.events.clear();
        output.timestamp_ns = input != nullptr ? input->timestamp_ns : uint64_t{0};
        output.sequence_number = input != nullptr
            ? input->sequence_number
            : uint64_t{0};
        uint32_t dropped = 0;

        if (valid_transport(transport)) {
            publish_due(*transport, frame_count, output, dropped);
        }
        if (input != nullptr) {
            for (const auto& event : input->events) {
                if (event.sample_offset >= frame_count
                    || event.quantization > CommRaT::Messages::DECK_QUANTIZE_BAR) {
                    ++dropped;
                    continue;
                }
                if (event.quantization
                    == CommRaT::Messages::DECK_QUANTIZE_IMMEDIATE) {
                    insert_event(event, output, dropped);
                    continue;
                }
                if (!valid_transport(transport)) {
                    queue_event(
                        event,
                        std::numeric_limits<double>::quiet_NaN(),
                        dropped);
                    continue;
                }
                schedule(event, *transport, frame_count, output, dropped);
            }
        }
        return {
            .pending_count = static_cast<uint32_t>(pending_count_),
            .dropped_count = dropped,
        };
    }

    void reset() noexcept {
        pending_count_ = 0;
    }

private:
    struct PendingEvent {
        Event event{};
        double target_beat{0.0};
    };

    static bool valid_transport(const TransportBlock* transport) noexcept {
        return transport != nullptr
            && std::isfinite(transport->beat_position)
            && transport->beat_position >= 0.0
            && std::isfinite(transport->tempo_bpm)
            && transport->tempo_bpm > 0.0
            && transport->sample_rate_hz > 0
            && transport->beats_per_bar > 0;
    }

    static bool advancing(const TransportBlock& transport) noexcept {
        return transport.state == CommRaT::Messages::TRANSPORT_PLAYING
            || transport.state == CommRaT::Messages::TRANSPORT_RECORDING;
    }

    static double beats_per_frame(const TransportBlock& transport) noexcept {
        return transport.tempo_bpm
            / (60.0 * static_cast<double>(transport.sample_rate_hz));
    }

    static double next_boundary(double beat, double quantum) noexcept {
        constexpr double boundary_epsilon = 1.0e-9;
        return std::ceil(beat / quantum - boundary_epsilon) * quantum;
    }

    static uint64_t boundary_offset(
        double target_beat,
        const TransportBlock& transport) noexcept {
        constexpr double boundary_epsilon = 1.0e-9;
        if (target_beat <= transport.beat_position + boundary_epsilon) {
            return 0;
        }
        const double offset = std::ceil(
            (target_beat - transport.beat_position)
                / beats_per_frame(transport)
            - boundary_epsilon);
        if (!std::isfinite(offset)
            || offset > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
            return std::numeric_limits<uint64_t>::max();
        }
        return static_cast<uint64_t>(offset);
    }

    static void insert_event(
        Event event,
        EventBlock& output,
        uint32_t& dropped) noexcept {
        if (output.events.size() >= EventBlock::MAX_EVENTS) {
            ++dropped;
            return;
        }
        output.events.push_back(event);
        std::size_t index = output.events.size() - 1;
        while (index > 0
            && output.events[index].sample_offset
                < output.events[index - 1].sample_offset) {
            std::swap(output.events[index], output.events[index - 1]);
            --index;
        }
    }

    void schedule(
        Event event,
        const TransportBlock& transport,
        uint32_t frame_count,
        EventBlock& output,
        uint32_t& dropped) noexcept {
        const double event_beat = transport.beat_position
            + static_cast<double>(event.sample_offset)
                * beats_per_frame(transport);
        const double quantum = event.quantization
                == CommRaT::Messages::DECK_QUANTIZE_BAR
            ? static_cast<double>(transport.beats_per_bar)
            : 1.0;
        const double target_beat = next_boundary(event_beat, quantum);
        const uint64_t offset = boundary_offset(target_beat, transport);
        if (advancing(transport) && offset < frame_count) {
            event.sample_offset = static_cast<uint32_t>(offset);
            insert_event(event, output, dropped);
            return;
        }
        queue_event(event, target_beat, dropped);
    }

    void queue_event(
        Event event,
        double target_beat,
        uint32_t& dropped) noexcept {
        if (pending_count_ >= pending_.size()) {
            ++dropped;
            return;
        }
        pending_[pending_count_++] = {
            .event = event,
            .target_beat = target_beat,
        };
    }

    void publish_due(
        const TransportBlock& transport,
        uint32_t frame_count,
        EventBlock& output,
        uint32_t& dropped) noexcept {
        std::size_t retained = 0;
        for (std::size_t index = 0; index < pending_count_; ++index) {
            auto pending = pending_[index];
            if (!std::isfinite(pending.target_beat)) {
                const double event_beat = transport.beat_position
                    + static_cast<double>(pending.event.sample_offset)
                        * beats_per_frame(transport);
                const double quantum = pending.event.quantization
                        == CommRaT::Messages::DECK_QUANTIZE_BAR
                    ? static_cast<double>(transport.beats_per_bar)
                    : 1.0;
                pending.target_beat = next_boundary(event_beat, quantum);
            }
            const uint64_t offset = boundary_offset(
                pending.target_beat, transport);
            if (advancing(transport) && offset < frame_count) {
                pending.event.sample_offset = static_cast<uint32_t>(offset);
                insert_event(pending.event, output, dropped);
            } else {
                pending_[retained++] = pending;
            }
        }
        pending_count_ = retained;
    }

    std::array<PendingEvent, EventBlock::MAX_EVENTS> pending_{};
    std::size_t pending_count_{0};
};

} // namespace musicrat::dsp