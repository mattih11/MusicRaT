#pragma once

#include <musicrat/protocol/audio_block.hpp>

#include <concepts>
#include <cstdint>

namespace musicrat::backends::media {

enum class MediaCodec : uint8_t {
    Unknown,
    PcmWav,
    Flac,
    Mp3,
    Aac,
    Opus,
};

enum class DecodeResult : uint8_t {
    Data,
    EndOfStream,
    Error,
};

enum class DecodeError : uint8_t {
    None,
    InvalidArgument,
    FileOpen,
    FileIo,
    UnsupportedFormat,
    InvalidMetadata,
    ResourceExhausted,
    SeekFailed,
    CorruptData,
    DecoderFailure,
    PoolFailure,
};

template<typename Decoder>
concept DecoderBackend = requires(
    Decoder& decoder,
    const Decoder& const_decoder,
    const char* path,
    uint64_t media_frame,
    uint32_t frame_count,
    CommRaT::Messages::AudioBlock& block) {
    { decoder.open(path) } noexcept -> std::same_as<bool>;
    { decoder.close() } noexcept -> std::same_as<void>;
    { decoder.seek_frame(media_frame) } noexcept -> std::same_as<bool>;
    { const_decoder.current_frame() } noexcept -> std::convertible_to<uint64_t>;
    { const_decoder.last_error() } noexcept -> std::convertible_to<DecodeError>;
    { decoder.read(block, frame_count) } noexcept -> std::same_as<DecodeResult>;
    { const_decoder.metadata().sample_rate_hz } -> std::convertible_to<uint32_t>;
    { const_decoder.metadata().channel_count } -> std::convertible_to<uint16_t>;
    { const_decoder.metadata().frame_count } -> std::convertible_to<uint64_t>;
};

} // namespace musicrat::backends::media