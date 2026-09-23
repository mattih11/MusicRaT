#include <algorithm>
#include <array>
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

uint16_t read_u16(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return static_cast<uint16_t>(bytes[offset])
        | static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8U);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        return 1;
    }
    const auto expected_channels = argc == 4
        ? static_cast<uint16_t>(std::stoul(argv[3]))
        : uint16_t{1};

    const std::filesystem::path output{"/tmp/musicrat-tone.wav"};
    std::filesystem::remove(output);

    const std::string command = '"' + std::string{argv[1]} + "\" \""
        + argv[2] + "\" --duration-ms 300";
    if (std::system(command.c_str()) != 0) {
        return 2;
    }

    std::ifstream stream(output, std::ios::binary);
    const std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    if (bytes.size() < 44
        || !std::equal(bytes.begin(), bytes.begin() + 4, "RIFF")
        || !std::equal(bytes.begin() + 8, bytes.begin() + 12, "WAVE")
        || read_u32(bytes, 4) + 8 != bytes.size()
        || read_u16(bytes, 22) != expected_channels
        || read_u32(bytes, 24) != 48000
        || read_u32(bytes, 40) + 44 != bytes.size()) {
        return 3;
    }

    const auto data_bytes = read_u32(bytes, 40);
    if (data_bytes < 480 * sizeof(int16_t) * expected_channels
        || data_bytes > 48000 * sizeof(int16_t) * expected_channels
        || data_bytes % (sizeof(int16_t) * expected_channels) != 0) {
        return 4;
    }

    const bool has_signal = std::any_of(
        bytes.begin() + 44,
        bytes.end(),
        [](unsigned char byte) { return byte != 0; });
    if (!keep_test_artifacts()) {
        std::filesystem::remove(output);
    }
    return has_signal ? 0 : 5;
}