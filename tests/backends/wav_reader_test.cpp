#include <musicrat/backends/audio/wav_writer.hpp>
#include <musicrat/backends/media/wav_reader.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

bool keep_test_artifacts() noexcept {
    return std::getenv("MUSICRAT_KEEP_TEST_ARTIFACTS") != nullptr;
}

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.0001;
}

void append_u16(std::vector<unsigned char>& bytes, uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<unsigned char>& bytes, uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}

void append_text(std::vector<unsigned char>& bytes, const char* text) {
    bytes.insert(bytes.end(), text, text + 4);
}

void write_chunked_wav(const std::filesystem::path& path, uint16_t encoding) {
    std::vector<unsigned char> bytes{};
    append_text(bytes, "RIFF");
    append_u32(bytes, 48);
    append_text(bytes, "WAVE");
    append_text(bytes, "JUNK");
    append_u32(bytes, 1);
    bytes.push_back(0xAB);
    bytes.push_back(0);
    append_text(bytes, "fmt ");
    append_u32(bytes, 16);
    append_u16(bytes, encoding);
    append_u16(bytes, 1);
    append_u32(bytes, 48000);
    append_u32(bytes, 96000);
    append_u16(bytes, 2);
    append_u16(bytes, 16);
    append_text(bytes, "data");
    append_u32(bytes, 2);
    append_u16(bytes, 0x7FFF);

    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path()
        / "musicrat_wav_reader_test.wav";
    const auto invalid_path = std::filesystem::temp_directory_path()
        / "musicrat_invalid_wav_reader_test.wav";
    const auto chunked_path = std::filesystem::temp_directory_path()
        / "musicrat_chunked_wav_reader_test.wav";
    const auto unsupported_path = std::filesystem::temp_directory_path()
        / "musicrat_unsupported_wav_reader_test.wav";

    CommRaT::Messages::AudioBlock source{};
    source.sample_rate_hz = 48000.0;
    source.frame_count = 3;
    source.channel_count = 2;
    source.channels[0].push_back(-1.0);
    source.channels[0].push_back(0.0);
    source.channels[0].push_back(1.0);
    source.channels[1].push_back(0.5);
    source.channels[1].push_back(-0.5);
    source.channels[1].push_back(0.25);

    {
        musicrat::backends::audio::WavWriter writer{};
        assert(writer.open(path.c_str(), 48000, 2));
        assert(writer.write(source));
    }

    musicrat::backends::media::WavReader reader{};
    assert(reader.open(path.c_str()));
    assert(reader.metadata().sample_rate_hz == 48000);
    assert(reader.metadata().channel_count == 2);
    assert(reader.metadata().bits_per_sample == 16);
    assert(reader.metadata().frame_count == 3);

    CommRaT::Messages::AudioBlock decoded{};
    assert(reader.read(decoded, 2)
        == musicrat::backends::media::WavReadResult::Data);
    assert(decoded.frame_count == 2);
    assert(decoded.channel_count == 2);
    assert(decoded.flags == 0);
    assert(near(decoded.channels[0][0], -1.0));
    assert(near(decoded.channels[0][1], 0.0));
    assert(near(decoded.channels[1][0], 0.5));
    assert(near(decoded.channels[1][1], -0.5));

    assert(reader.read(decoded, 2)
        == musicrat::backends::media::WavReadResult::Data);
    assert(decoded.frame_count == 1);
    assert(decoded.flags & CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM);
    assert(near(decoded.channels[0][0], 1.0));
    assert(near(decoded.channels[1][0], 0.25));

    assert(reader.seek_frame(1));
    assert(reader.read(decoded, 1)
        == musicrat::backends::media::WavReadResult::Data);
    assert(decoded.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    assert(decoded.timestamp_ns == 20833);
    assert(near(decoded.channels[1][0], -0.5));

    assert(reader.seek_frame(reader.metadata().frame_count));
    assert(reader.read(decoded, 1)
        == musicrat::backends::media::WavReadResult::EndOfStream);
    assert(decoded.frame_count == 0);
    assert(decoded.flags == CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM);
    assert(!reader.seek_frame(reader.metadata().frame_count + 1));
    assert(reader.last_error()
        == musicrat::backends::media::DecodeError::InvalidArgument);

    {
        std::ofstream invalid{invalid_path, std::ios::binary | std::ios::trunc};
        invalid.write("RIFF", 4);
    }
    musicrat::backends::media::WavReader invalid_reader{};
    assert(!invalid_reader.open(invalid_path.c_str()));
    assert(invalid_reader.last_error()
        == musicrat::backends::media::DecodeError::FileIo);

    write_chunked_wav(chunked_path, 1);
    musicrat::backends::media::WavReader chunked_reader{};
    assert(chunked_reader.open(chunked_path.c_str()));
    assert(chunked_reader.metadata().frame_count == 1);
    assert(chunked_reader.read(decoded, 1)
        == musicrat::backends::media::WavReadResult::Data);
    assert(near(decoded.channels[0][0], 1.0));

    write_chunked_wav(unsupported_path, 3);
    musicrat::backends::media::WavReader unsupported_reader{};
    assert(!unsupported_reader.open(unsupported_path.c_str()));
    assert(unsupported_reader.last_error()
        == musicrat::backends::media::DecodeError::UnsupportedFormat);

    if (!keep_test_artifacts()) {
        std::filesystem::remove(path);
        std::filesystem::remove(invalid_path);
        std::filesystem::remove(chunked_path);
        std::filesystem::remove(unsupported_path);
    }
}