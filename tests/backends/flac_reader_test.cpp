#include <musicrat/backends/media/flac_reader.hpp>

#include <flac_fixture.hpp>

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

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path()
        / "musicrat_flac_reader_test.flac";
    const auto invalid_path = std::filesystem::temp_directory_path()
        / "musicrat_invalid_flac_reader_test.flac";
    const auto truncated_path = std::filesystem::temp_directory_path()
        / "musicrat_truncated_flac_reader_test.flac";
    const auto oversized_path = std::filesystem::temp_directory_path()
        / "musicrat_oversized_block_reader_test.flac";
    const std::vector<FLAC__int32> samples{
        -32768, 16384,
        0, -16384,
        32767, 8192,
        16384, -8192,
        -16384, 0,
        0, 32767,
    };
    assert(write_flac_fixture(path, 48000, 2, samples));

    musicrat::backends::media::FlacReader reader{};
    assert(reader.open(path.c_str()));
    assert(reader.metadata().sample_rate_hz == 48000);
    assert(reader.metadata().channel_count == 2);
    assert(reader.metadata().bits_per_sample == 16);
    assert(reader.metadata().frame_count == 6);
    assert(reader.metadata().maximum_block_frames
        <= CommRaT::Messages::AudioBlock::MAX_FRAMES);

    CommRaT::Messages::AudioBlock decoded{};
    assert(reader.read(decoded, 2)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.frame_count == 2);
    assert(decoded.channel_count == 2);
    assert(decoded.flags == 0);
    assert(near(decoded.channels[0][0], -1.0));
    assert(near(decoded.channels[0][1], 0.0));
    assert(near(decoded.channels[1][0], 0.5));
    assert(near(decoded.channels[1][1], -0.5));

    assert(reader.read(decoded, 4)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.frame_count == 4);
    assert(decoded.flags & CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM);
    assert(near(decoded.channels[0][0], 1.0));
    assert(near(decoded.channels[1][3], 1.0));

    assert(reader.read(decoded, 1)
        == musicrat::backends::media::DecodeResult::EndOfStream);
    assert(decoded.frame_count == 0);
    assert(decoded.flags == CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM);

    assert(reader.seek_frame(1));
    assert(reader.read(decoded, 2)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    assert(decoded.timestamp_ns == 20833);
    assert(near(decoded.channels[0][0], 0.0));
    assert(near(decoded.channels[0][1], 1.0));
    assert(!reader.seek_frame(7));
    assert(reader.last_error()
        == musicrat::backends::media::DecodeError::InvalidArgument);

    {
        std::ofstream invalid{invalid_path, std::ios::binary | std::ios::trunc};
        invalid << "not a FLAC stream";
    }
    musicrat::backends::media::FlacReader invalid_reader{};
    assert(!invalid_reader.open(invalid_path.c_str()));
    assert(invalid_reader.last_error()
        == musicrat::backends::media::DecodeError::CorruptData);

    std::filesystem::copy_file(
        path,
        truncated_path,
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::resize_file(
        truncated_path,
        std::filesystem::file_size(truncated_path) - 4);
    musicrat::backends::media::FlacReader truncated_reader{};
    assert(truncated_reader.open(truncated_path.c_str()));
    assert(truncated_reader.read(decoded, 6)
        == musicrat::backends::media::DecodeResult::Error);
    assert(truncated_reader.last_error()
        == musicrat::backends::media::DecodeError::CorruptData);

    std::vector<FLAC__int32> oversized_samples(8192, 0);
    assert(write_flac_fixture(
        oversized_path, 48000, 1, oversized_samples, 8192));
    musicrat::backends::media::FlacReader oversized_reader{};
    assert(!oversized_reader.open(oversized_path.c_str()));
    assert(oversized_reader.last_error()
        == musicrat::backends::media::DecodeError::InvalidMetadata);

    if (!keep_test_artifacts()) {
        std::filesystem::remove(path);
        std::filesystem::remove(invalid_path);
        std::filesystem::remove(truncated_path);
        std::filesystem::remove(oversized_path);
    }
}