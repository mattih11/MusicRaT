#include <musicrat/protocol/transport.hpp>

#include <sertial/message.hpp>

#include <cassert>

int main() {
    CommRaT::Messages::TransportBlock original{};
    original.beat_position = 7.5;
    original.tempo_bpm = 128.0;
    original.transport_frame = 135000;
    original.timestamp_ns = 1234;
    original.sequence_number = 9;
    original.sample_rate_hz = 48000;
    original.beats_per_bar = 4;
    original.beat_unit = 4;
    original.state = CommRaT::Messages::TRANSPORT_PLAYING;
    original.flags = CommRaT::Messages::TRANSPORT_DISCONTINUITY;

    const auto serialized = sertial::Message<CommRaT::Messages::TransportBlock>
        ::serialize(original);
    const auto restored = sertial::Message<CommRaT::Messages::TransportBlock>
        ::deserialize(serialized.view());
    assert(restored);
    assert(restored->beat_position == original.beat_position);
    assert(restored->tempo_bpm == original.tempo_bpm);
    assert(restored->transport_frame == original.transport_frame);
    assert(restored->timestamp_ns == original.timestamp_ns);
    assert(restored->sequence_number == original.sequence_number);
    assert(restored->sample_rate_hz == original.sample_rate_hz);
    assert(restored->beats_per_bar == original.beats_per_bar);
    assert(restored->beat_unit == original.beat_unit);
    assert(restored->state == original.state);
    assert(restored->flags == original.flags);
}