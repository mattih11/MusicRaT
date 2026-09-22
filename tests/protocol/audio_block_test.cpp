#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <sertial/message.hpp>

#include <cassert>
#include <cmath>
#include <type_traits>

int main() {
    using CommRaT::Messages::AudioBlock;

    static_assert(AudioBlock::MAX_CHANNELS > 0);
    static_assert(AudioBlock::MAX_FRAMES > 0);
    static_assert(std::is_same_v<AudioBlock::Sample, musicrat::config::sample_type>);

    AudioBlock original{};
    original.sample_rate_hz = 48000.0;
    original.timestamp_ns = 123456789;
    original.sequence_number = 42;
    original.frame_count = 2;
    original.channel_count = 1;
    original.channels[0].push_back(AudioBlock::Sample{0.25});
    original.channels[0].push_back(AudioBlock::Sample{-0.25});

    const auto serialized = sertial::Message<AudioBlock>::serialize(original);
    const auto restored = sertial::Message<AudioBlock>::deserialize(serialized.view());

    assert(restored);
    assert(restored->sample_rate_hz == original.sample_rate_hz);
    assert(restored->timestamp_ns == original.timestamp_ns);
    assert(restored->sequence_number == original.sequence_number);
    assert(restored->frame_count == original.frame_count);
    assert(restored->channel_count == 1);
    assert(restored->channels[0].size() == 2);
    assert(std::abs(restored->channels[0][0] - AudioBlock::Sample{0.25})
        < AudioBlock::Sample{1.0e-6});

    assert(musicrat::validate_audio_block(*restored)
        == musicrat::AudioBlockValidationError::None);
    auto invalid = *restored;
    invalid.channels[1].push_back(AudioBlock::Sample{0});
    assert(musicrat::validate_audio_block(invalid)
        == musicrat::AudioBlockValidationError::InactiveChannelNotEmpty);
}