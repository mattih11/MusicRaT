#pragma once

#include <musicrat/backends/media/decoder.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace musicrat::backends::media {

struct MediaMetadata {
    MediaCodec codec{MediaCodec::Unknown};
    uint32_t sample_rate_hz{0};
    uint16_t channel_count{0};
    uint64_t frame_count{0};
};

template<typename Decoder>
concept DispatchableDecoderBackend = DecoderBackend<Decoder>
    && requires {
        { Decoder::codec } -> std::convertible_to<MediaCodec>;
    };

template<DispatchableDecoderBackend... Decoders>
class DecoderDispatcher {
public:
    DecoderDispatcher() = default;

    DecoderDispatcher(const DecoderDispatcher&) = delete;
    DecoderDispatcher& operator=(const DecoderDispatcher&) = delete;

    [[nodiscard]] bool open(const char* path) noexcept {
        close();
        last_error_ = DecodeError::None;
        return try_open<1>(path);
    }

    void close() noexcept {
        std::visit([](auto& decoder) noexcept {
            using Decoder = std::remove_cvref_t<decltype(decoder)>;
            if constexpr (!std::is_same_v<Decoder, std::monostate>) {
                decoder.close();
            }
        }, decoder_);
        decoder_.template emplace<0>();
        metadata_ = {};
        last_error_ = DecodeError::None;
    }

    [[nodiscard]] bool seek_frame(uint64_t media_frame) noexcept {
        const bool sought = std::visit([media_frame](auto& decoder) noexcept {
            using Decoder = std::remove_cvref_t<decltype(decoder)>;
            if constexpr (std::is_same_v<Decoder, std::monostate>) {
                return false;
            } else {
                return decoder.seek_frame(media_frame);
            }
        }, decoder_);
        last_error_ = std::visit([](const auto& decoder) noexcept {
            using Decoder = std::remove_cvref_t<decltype(decoder)>;
            if constexpr (std::is_same_v<Decoder, std::monostate>) {
                return DecodeError::InvalidArgument;
            } else {
                return decoder.last_error();
            }
        }, decoder_);
        return sought;
    }

    [[nodiscard]] uint64_t current_frame() const noexcept {
        return std::visit([](const auto& decoder) noexcept -> uint64_t {
            using Decoder = std::remove_cvref_t<decltype(decoder)>;
            if constexpr (std::is_same_v<Decoder, std::monostate>) {
                return 0;
            } else {
                return decoder.current_frame();
            }
        }, decoder_);
    }

    [[nodiscard]] const MediaMetadata& metadata() const noexcept {
        return metadata_;
    }

    [[nodiscard]] DecodeError last_error() const noexcept {
        return last_error_;
    }

    [[nodiscard]] DecodeResult read(
        CommRaT::Messages::AudioBlock& block,
        uint32_t requested_frames) noexcept {
        const auto result = std::visit([&](auto& decoder) noexcept {
            using Decoder = std::remove_cvref_t<decltype(decoder)>;
            if constexpr (std::is_same_v<Decoder, std::monostate>) {
                block = {};
                block.flags = CommRaT::Messages::AUDIO_BLOCK_INVALID;
                return DecodeResult::Error;
            } else {
                return decoder.read(block, requested_frames);
            }
        }, decoder_);
        last_error_ = std::visit([](const auto& decoder) noexcept {
            using Decoder = std::remove_cvref_t<decltype(decoder)>;
            if constexpr (std::is_same_v<Decoder, std::monostate>) {
                return DecodeError::InvalidArgument;
            } else {
                return decoder.last_error();
            }
        }, decoder_);
        return result;
    }

private:
    template<std::size_t Index>
    [[nodiscard]] bool try_open(const char* path) noexcept {
        if constexpr (Index > sizeof...(Decoders)) {
            return false;
        } else {
            auto& decoder = decoder_.template emplace<Index>();
            if (decoder.open(path)) {
                const auto& backend_metadata = decoder.metadata();
                metadata_ = {
                    .codec = decoder.codec,
                    .sample_rate_hz = backend_metadata.sample_rate_hz,
                    .channel_count = backend_metadata.channel_count,
                    .frame_count = backend_metadata.frame_count,
                };
                last_error_ = DecodeError::None;
                return true;
            }
            last_error_ = decoder.last_error();
            decoder.close();
            decoder_.template emplace<0>();
            return try_open<Index + 1>(path);
        }
    }

    std::variant<std::monostate, Decoders...> decoder_{};
    MediaMetadata metadata_{};
    DecodeError last_error_{DecodeError::None};
};

} // namespace musicrat::backends::media