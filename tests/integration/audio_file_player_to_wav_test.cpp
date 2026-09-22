#include <musicrat/backends/audio/wav_writer.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool keep_test_artifacts() noexcept {
    return std::getenv("MUSICRAT_KEEP_TEST_ARTIFACTS") != nullptr;
}

uint32_t read_u32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return static_cast<uint32_t>(bytes[offset])
        | (static_cast<uint32_t>(bytes[offset + 1]) << 8U)
        | (static_cast<uint32_t>(bytes[offset + 2]) << 16U)
        | (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return 1;
    }

    const std::filesystem::path input{"/tmp/musicrat-player-input.wav"};
    const std::filesystem::path output{"/tmp/musicrat-player-output.wav"};
    std::filesystem::remove(input);
    std::filesystem::remove(output);

    CommRaT::Messages::AudioBlock source{};
    source.sample_rate_hz = 48000.0;
    source.frame_count = 4000;
    source.channel_count = 1;
    for (uint32_t frame = 0; frame < source.frame_count; ++frame) {
        source.channels[0].push_back((frame % 64) < 32 ? 0.25 : -0.25);
    }
    {
        musicrat::backends::audio::WavWriter writer{};
        if (!writer.open(input.c_str(), 48000, 1)) {
            return 2;
        }
        for (uint32_t block = 0; block < 20; ++block) {
            if (!writer.write(source)) {
                return 2;
            }
        }
    }

    const std::string command = '"' + std::string{argv[1]} + "\" \""
        + argv[2] + "\" --duration-ms 300";
    if (std::system(command.c_str()) != 0) {
        return 3;
    }

    std::ifstream stream(output, std::ios::binary);
    const std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    const bool valid = bytes.size() >= 44
        && std::equal(bytes.begin(), bytes.begin() + 4, "RIFF")
        && std::equal(bytes.begin() + 8, bytes.begin() + 12, "WAVE")
        && read_u32(bytes, 4) + 8 == bytes.size()
        && read_u32(bytes, 24) == 48000
        && read_u32(bytes, 40) + 44 == bytes.size()
        && std::any_of(
            bytes.begin() + 44,
            bytes.end(),
            [](unsigned char byte) { return byte != 0; });

    if (!keep_test_artifacts()) {
        std::filesystem::remove(input);
        std::filesystem::remove(output);
    }
    return valid ? 0 : 4;
}