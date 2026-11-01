#include <musicrat/backends/audio/wav_writer.hpp>
#include <musicrat/backends/media/media_decoder.hpp>

#if MUSICRAT_HAS_FLAC
#include <flac_fixture.hpp>
#endif
#if MUSICRAT_HAS_MP3
#include <mp3_fixture.hpp>
#endif
#if MUSICRAT_HAS_AAC
#include <aac_fixture.hpp>
#endif
#if MUSICRAT_HAS_OPUS
#include <opus_fixture.hpp>
#endif

#include <cassert>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <numbers>
#include "../support/directory_fixture.hpp"

namespace {

struct RejectingMetadata {
    uint32_t sample_rate_hz{0};
    uint16_t channel_count{0};
    uint64_t frame_count{0};
};

class RejectingDecoder {
public:
    static constexpr auto codec = musicrat::backends::media::MediaCodec::Flac;
    static inline uint32_t open_attempts = 0;

    [[nodiscard]] bool open(const char*) noexcept {
        ++open_attempts;
        return false;
    }

    void close() noexcept {}

    [[nodiscard]] bool seek_frame(uint64_t) noexcept {
        return false;
    }

    [[nodiscard]] uint64_t current_frame() const noexcept {
        return 0;
    }

    [[nodiscard]] musicrat::backends::media::DecodeError last_error() const noexcept {
        return musicrat::backends::media::DecodeError::UnsupportedFormat;
    }

    [[nodiscard]] const RejectingMetadata& metadata() const noexcept {
        return metadata_;
    }

    [[nodiscard]] musicrat::backends::media::DecodeResult read(
        CommRaT::Messages::AudioBlock&,
        uint32_t) noexcept {
        return musicrat::backends::media::DecodeResult::Error;
    }

private:
    RejectingMetadata metadata_{};
};

static_assert(musicrat::backends::media::DispatchableDecoderBackend<RejectingDecoder>);

} // namespace

int main() {
    const auto wav_path = std::filesystem::temp_directory_path()
        / "musicrat_media_decoder_test.wav";
    const auto invalid_path = std::filesystem::temp_directory_path()
        / "musicrat_media_decoder_test.invalid";
#if MUSICRAT_HAS_FLAC
    const auto flac_path = std::filesystem::temp_directory_path()
        / "musicrat_media_decoder_test.flac";
#endif
#if MUSICRAT_HAS_MP3
    const auto mp3_path = std::filesystem::temp_directory_path()
        / "musicrat_media_decoder_test.mp3";
#endif
#if MUSICRAT_HAS_AAC
    const auto aac_path = std::filesystem::temp_directory_path()
        / "musicrat_media_decoder_test.aac";
#endif
#if MUSICRAT_HAS_OPUS
    const auto opus_path = std::filesystem::temp_directory_path()
        / "musicrat_media_decoder_test.opus";
#endif

    CommRaT::Messages::AudioBlock source{};
    source.sample_rate_hz = 44100.0;
    source.frame_count = 2;
    source.channel_count = 1;
    source.channels[0].push_back(-0.5);
    source.channels[0].push_back(0.5);
    {
        musicrat::backends::audio::WavWriter writer{};
        assert(writer.open(wav_path.c_str(), 44100, 1));
        assert(writer.write(source));
    }
    {
        std::ofstream invalid{invalid_path, std::ios::binary};
        invalid << "not an audio file";
    }
#if MUSICRAT_HAS_FLAC
    assert(write_flac_fixture(flac_path, 48000, 1, {-32768, 0, 32767}));
#endif
#if MUSICRAT_HAS_MP3
    assert(write_mp3_fixture(mp3_path));
#endif
#if MUSICRAT_HAS_AAC
    assert(write_aac_fixture(aac_path));
#endif
#if MUSICRAT_HAS_OPUS
    assert(write_opus_fixture(opus_path));
#endif

    musicrat::backends::media::MediaDecoder decoder{};
    assert(!decoder.open(invalid_path.c_str()));
    assert(decoder.last_error()
        == musicrat::backends::media::DecodeError::UnsupportedFormat);
    assert(decoder.metadata().codec
        == musicrat::backends::media::MediaCodec::Unknown);

    assert(decoder.open(wav_path.c_str()));
    assert(decoder.last_error() == musicrat::backends::media::DecodeError::None);
    assert(decoder.metadata().codec
        == musicrat::backends::media::MediaCodec::PcmWav);
    assert(decoder.metadata().sample_rate_hz == 44100);
    assert(decoder.metadata().channel_count == 1);
    assert(decoder.metadata().frame_count == 2);

    CommRaT::Messages::AudioBlock decoded{};
    assert(decoder.read(decoded, 2)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.frame_count == 2);
    decoder.close();

#if MUSICRAT_HAS_FLAC
    assert(decoder.open(flac_path.c_str()));
    assert(decoder.metadata().codec
        == musicrat::backends::media::MediaCodec::Flac);
    assert(decoder.metadata().sample_rate_hz == 48000);
    assert(decoder.metadata().channel_count == 1);
    assert(decoder.metadata().frame_count == 3);
    assert(decoder.read(decoded, 3)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.frame_count == 3);
    decoder.close();
#endif

#if MUSICRAT_HAS_MP3
    assert(decoder.open(mp3_path.c_str()));
    assert(decoder.metadata().codec
        == musicrat::backends::media::MediaCodec::Mp3);
    assert(decoder.metadata().sample_rate_hz == 48000);
    assert(decoder.metadata().channel_count == 1);
    assert(decoder.metadata().frame_count > 100'000);
    assert(decoder.read(decoded, 3)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.frame_count == 3);
    decoder.close();
#endif

#if MUSICRAT_HAS_AAC
    assert(decoder.open(aac_path.c_str()));
    assert(decoder.metadata().codec
        == musicrat::backends::media::MediaCodec::Aac);
    assert(decoder.metadata().sample_rate_hz == 48000);
    assert(decoder.metadata().channel_count == 1);
    assert(decoder.metadata().frame_count > 150'000);
    assert(decoder.read(decoded, 3)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.frame_count == 3);
    decoder.close();
#endif

#if MUSICRAT_HAS_OPUS
    assert(decoder.open(opus_path.c_str()));
    assert(decoder.metadata().codec
        == musicrat::backends::media::MediaCodec::Opus);
    assert(decoder.metadata().sample_rate_hz == 48000);
    assert(decoder.metadata().channel_count == 1);
    assert(decoder.metadata().frame_count >= 48'000);
    assert(decoder.read(decoded, 3)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.frame_count == 3);
    decoder.close();
#endif

    using FallbackDecoder = musicrat::backends::media::DecoderDispatcher<
        RejectingDecoder,
        musicrat::backends::media::WavReader>;
    RejectingDecoder::open_attempts = 0;
    FallbackDecoder fallback{};
    assert(fallback.open(wav_path.c_str()));
    assert(RejectingDecoder::open_attempts == 1);
    assert(fallback.metadata().codec
        == musicrat::backends::media::MediaCodec::PcmWav);

    // MPEG sync patterns in PCM must not win over a valid WAV container.
    musicrat::testing::TestDirectory fixture;
    const auto tone_path = fixture.path / "tone.wav";
    {
        musicrat::backends::audio::WavWriter writer;
        assert(writer.open(tone_path.c_str(), 44100, 2));
        for (uint32_t start = 0; start < 44100;) {
            source = {};
            source.sample_rate_hz = 44100;
            source.channel_count = 2;
            source.frame_count = std::min<uint32_t>(127, 44100 - start);
            for (uint32_t frame = 0; frame < source.frame_count; ++frame) {
                const auto value = static_cast<CommRaT::Messages::AudioBlock::Sample>(
                    0.6 * std::sin(2 * std::numbers::pi * 60 * (start + frame) / 44100));
                source.channels[0].push_back(value);
                source.channels[1].push_back(-value);
            }
            assert(writer.write(source));
            start += source.frame_count;
        }
    }
    assert(decoder.open(tone_path.c_str()));
    assert(decoder.metadata().codec == musicrat::backends::media::MediaCodec::PcmWav);
    assert(decoder.metadata().frame_count == 44100 && decoder.metadata().channel_count == 2);

    std::filesystem::remove(wav_path);
    std::filesystem::remove(invalid_path);
#if MUSICRAT_HAS_FLAC
    std::filesystem::remove(flac_path);
#endif
#if MUSICRAT_HAS_MP3
    std::filesystem::remove(mp3_path);
#endif
#if MUSICRAT_HAS_AAC
    std::filesystem::remove(aac_path);
#endif
#if MUSICRAT_HAS_OPUS
    std::filesystem::remove(opus_path);
#endif
}