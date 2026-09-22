#include <musicrat/backends/audio/wav_writer.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace {

bool keep_test_artifacts() noexcept {
    return std::getenv("MUSICRAT_KEEP_TEST_ARTIFACTS") != nullptr;
}

uint32_t read_u32(const std::array<unsigned char, 52>& bytes, std::size_t offset) {
    return static_cast<uint32_t>(bytes[offset])
        | (static_cast<uint32_t>(bytes[offset + 1]) << 8U)
        | (static_cast<uint32_t>(bytes[offset + 2]) << 16U)
        | (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path()
        / "musicrat_wav_writer_test.wav";

    CommRaT::Messages::AudioBlock block{};
    block.sample_rate_hz = 48000.0;
    block.frame_count = 2;
    block.channel_count = 2;
    block.channels[0].push_back(-1.0);
    block.channels[0].push_back(1.0);
    block.channels[1].push_back(0.0);
    block.channels[1].push_back(0.5);

    {
        musicrat::backends::audio::WavWriter writer{};
        assert(writer.open(path.c_str(), 48000, 2));
        assert(writer.write(block));
        writer.close();
    }

    assert(std::filesystem::file_size(path) == 52);
    std::array<unsigned char, 52> bytes{};
    std::ifstream stream(path, std::ios::binary);
    stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    assert(stream.gcount() == static_cast<std::streamsize>(bytes.size()));
    assert(bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F');
    assert(bytes[8] == 'W' && bytes[9] == 'A' && bytes[10] == 'V' && bytes[11] == 'E');
    assert(read_u32(bytes, 24) == 48000);
    assert(read_u32(bytes, 40) == 8);
    assert(bytes[44] == 0x00 && bytes[45] == 0x80);
    assert(bytes[46] == 0x00 && bytes[47] == 0x00);

    if (!keep_test_artifacts()) {
        std::filesystem::remove(path);
    }
}