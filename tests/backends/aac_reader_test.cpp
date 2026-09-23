#include <musicrat/backends/media/aac_reader.hpp>

#include <aac_fixture.hpp>

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
        / "musicrat_aac_reader_test.aac";
    const auto invalid_path = std::filesystem::temp_directory_path()
        / "musicrat_invalid_aac_reader_test.aac";
    assert(write_aac_fixture(path));

    musicrat::backends::media::AacReader reader{};
    assert(reader.open(path.c_str()));
    assert(reader.metadata().sample_rate_hz == 48000);
    assert(reader.metadata().channel_count == 1);
    assert(reader.metadata().frame_count > 150'000);

    CommRaT::Messages::AudioBlock decoded{};
    assert(reader.read(decoded, 128)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.frame_count == 128);
    assert(decoded.channel_count == 1);
    assert(decoded.sample_rate_hz == 48000.0);
    assert(decoded.timestamp_ns == 0);
    assert(decoded.flags == 0);

    bool has_audio = std::any_of(
        decoded.channels[0].begin(), decoded.channels[0].end(),
        [](auto sample) { return sample != 0; });
    for (unsigned attempt = 0; attempt < 16 && !has_audio; ++attempt) {
        assert(reader.read(decoded, 128)
            == musicrat::backends::media::DecodeResult::Data);
        has_audio = std::any_of(
            decoded.channels[0].begin(), decoded.channels[0].end(),
            [](auto sample) { return sample != 0; });
    }
    assert(has_audio);

    assert(reader.seek_frame(48'000));
    assert(reader.read(decoded, 128)
        == musicrat::backends::media::DecodeResult::Data);
    assert(decoded.timestamp_ns == 1'000'000'000);
    assert(decoded.flags & CommRaT::Messages::AUDIO_BLOCK_DISCONTINUITY);
    assert(!reader.seek_frame(reader.metadata().frame_count + 1));
    assert(reader.last_error()
        == musicrat::backends::media::DecodeError::InvalidArgument);

    assert(reader.seek_frame(reader.metadata().frame_count));
    assert(reader.read(decoded, 1)
        == musicrat::backends::media::DecodeResult::EndOfStream);
    assert(decoded.flags == CommRaT::Messages::AUDIO_BLOCK_END_OF_STREAM);

    {
        std::ofstream invalid{invalid_path, std::ios::binary | std::ios::trunc};
        invalid << "not an AAC stream";
    }
    musicrat::backends::media::AacReader invalid_reader{};
    assert(!invalid_reader.open(invalid_path.c_str()));

    if (!keep_test_artifacts()) {
        std::filesystem::remove(path);
        std::filesystem::remove(invalid_path);
    }
}