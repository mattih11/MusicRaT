#include <musicrat/protocol/telemetry.hpp>

#include <sertial/message.hpp>

#include <cassert>

int main() {
    using CommRaT::Messages::LevelMeterBlock;

    LevelMeterBlock original{};
    original.peak[0] = 0.75;
    original.rms[0] = 0.5;
    original.clipped[0] = 1;
    original.timestamp_ns = 1234;
    original.sequence_number = 9;
    original.channel_count = 1;

    const auto serialized = sertial::Message<LevelMeterBlock>::serialize(original);
    const auto restored = sertial::Message<LevelMeterBlock>::deserialize(serialized.view());

    assert(restored);
    assert(restored->peak[0] == original.peak[0]);
    assert(restored->rms[0] == original.rms[0]);
    assert(restored->clipped[0] == 1);
    assert(restored->timestamp_ns == original.timestamp_ns);
    assert(restored->sequence_number == original.sequence_number);
    assert(restored->channel_count == 1);
}