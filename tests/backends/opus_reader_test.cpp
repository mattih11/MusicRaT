#include <musicrat/backends/media/opus_reader.hpp>

#include <opus_fixture.hpp>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

bool keep_test_artifacts() noexcept {
    return std::getenv("MUSICRAT_KEEP_TEST_ARTIFACTS") != nullptr;
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path()
        / "musicrat_opus_reader_test.opus";
    const auto invalid_path = std::filesystem::temp_directory_path()
        / "musicrat_invalid_opus_reader_test.opus";
    assert(write_opus_fixture(path));

    musicrat::backends::media::OpusReader reader{};
    assert(reader.open(path.c_str()));
    assert(reader.metadata().sample_rate_hz == 48000);
    assert(reader.metadata().channel_count == 1);
    assert(reader.metadata().frame_count >= 48'000);

    CommRaT::Messages::AudioBlock decoded{};
    assert(reader.read(decoded, 128)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.frame_count == 128);
    assert(decoded.channel_count == 1);
    assert(decoded.sample_rate_hz == 48000.0);
    assert(decoded.timestamp_ns == 0);
    assert(std::any_of(
        decoded.channels[0].begin(), decoded.channels[0].end(),
        [](auto sample) { return sample != 0; }));

    assert(reader.seek_frame(24'000));
    assert(reader.read(decoded, 128)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.timestamp_ns == 500'000'000);
    assert(decoded.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    assert(!reader.seek_frame(reader.metadata().frame_count + 1));
    assert(reader.last_error()
        == musicrat::backends::media::DecodeError::InvalidArgument);

    assert(reader.seek_frame(reader.metadata().frame_count));
    assert(reader.read(decoded, 1)
        == musicrat::backends::media::DecodeResult::EndOfStream);

    {
        std::ofstream invalid{invalid_path, std::ios::binary | std::ios::trunc};
        invalid << "not an Opus stream";
    }
    musicrat::backends::media::OpusReader invalid_reader{};
    assert(!invalid_reader.open(invalid_path.c_str()));

    if (!keep_test_artifacts()) {
        std::filesystem::remove(path);
        std::filesystem::remove(invalid_path);
    }
}