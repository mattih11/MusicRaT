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

int16_t read_i16(const std::vector<unsigned char>& bytes, std::size_t offset) {
    const auto value = static_cast<uint16_t>(bytes[offset])
        | (static_cast<uint16_t>(bytes[offset + 1]) << 8U);
    return static_cast<int16_t>(value);
}

bool has_rate_doubled_square_wave(const std::vector<unsigned char>& bytes) {
    if (bytes.size() < 44) {
        return false;
    }

    std::vector<std::size_t> transitions{};
    int previous_sign = 0;
    std::size_t previous_transition = 0;
    for (std::size_t offset = 44; offset + 1 < bytes.size(); offset += 2) {
        const auto sample = read_i16(bytes, offset);
        const int sign = sample > 1000 ? 1 : sample < -1000 ? -1 : 0;
        if (sign == 0) {
            continue;
        }
        const std::size_t frame = (offset - 44) / 2;
        if (previous_sign != 0 && sign != previous_sign) {
            if (previous_transition != 0) {
                transitions.push_back(frame - previous_transition);
            }
            previous_transition = frame;
        }
        previous_sign = sign;
    }
    if (transitions.size() < 8) {
        return false;
    }
    return std::count_if(
        transitions.begin(), transitions.end(),
        [](std::size_t length) { return length >= 15 && length <= 17; }) >= 8;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return 1;
    }

    const std::filesystem::path input{"/tmp/musicrat-action-player-input.wav"};
    const std::filesystem::path output{"/tmp/musicrat-action-player-output.wav"};
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
    const bool valid = has_rate_doubled_square_wave(bytes);

    if (!keep_test_artifacts()) {
        std::filesystem::remove(input);
        std::filesystem::remove(output);
    }
    return valid ? 0 : 4;
}
