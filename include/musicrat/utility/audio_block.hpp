#pragma once

#include <musicrat/protocol/audio_block.hpp>

#include <cmath>
#include <cstdint>

namespace musicrat {

enum class AudioBlockValidationError : uint8_t {
    None,
    InvalidSampleRate,
    TooManyChannels,
    TooManyFrames,
    ActiveChannelSizeMismatch,
    InactiveChannelNotEmpty,
};

[[nodiscard]] inline AudioBlockValidationError validate_audio_block(
    const CommRaT::Messages::AudioBlock& block) noexcept {
    if (!std::isfinite(block.sample_rate_hz) || block.sample_rate_hz <= 0.0) {
        return AudioBlockValidationError::InvalidSampleRate;
    }
    if (block.channel_count > CommRaT::Messages::AudioBlock::MAX_CHANNELS) {
        return AudioBlockValidationError::TooManyChannels;
    }
    if (block.frame_count > CommRaT::Messages::AudioBlock::MAX_FRAMES) {
        return AudioBlockValidationError::TooManyFrames;
    }
    for (uint16_t channel = 0; channel < block.channel_count; ++channel) {
        if (block.channels[channel].size() != block.frame_count) {
            return AudioBlockValidationError::ActiveChannelSizeMismatch;
        }
    }
    for (std::size_t channel = block.channel_count;
         channel < CommRaT::Messages::AudioBlock::MAX_CHANNELS;
         ++channel) {
        if (!block.channels[channel].empty()) {
            return AudioBlockValidationError::InactiveChannelNotEmpty;
        }
    }
    return AudioBlockValidationError::None;
}

inline void clear_audio_channels(CommRaT::Messages::AudioBlock& block) noexcept {
    for (auto& channel : block.channels) {
        channel.clear();
    }
}

inline void copy_audio_metadata(
    const CommRaT::Messages::AudioBlock& input,
    CommRaT::Messages::AudioBlock& output) noexcept {
    output.sample_rate_hz = input.sample_rate_hz;
    output.timestamp_ns = input.timestamp_ns;
    output.sequence_number = input.sequence_number;
    output.frame_count = input.frame_count;
    output.channel_count = input.channel_count;
    output.flags = input.flags;
}

inline void mark_invalid_audio_block(CommRaT::Messages::AudioBlock& block) noexcept {
    clear_audio_channels(block);
    block.frame_count = 0;
    block.channel_count = 0;
    block.flags |= CommRaT::Messages::AUDIO_BLOCK_INVALID;
}

} // namespace musicrat