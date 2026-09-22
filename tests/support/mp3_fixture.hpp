#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

inline bool write_mp3_fixture(const std::filesystem::path& path) {
    constexpr std::size_t encoded_frame_bytes = 192;
    constexpr std::size_t audio_frame_repetitions = 30;
    constexpr std::string_view encoded{
        "//tUwAAAAAAAAAAAAAAAAAAAAAAASW5mbwAAAA8AAAAEAAADwABmZmZmZmZmZmZmZmZmZmZmZmZmZmZm"
        "ZmaZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZmZzMzMzMzMzMzMzMzMzMzMzMzMzMzMzMzMzP//////////"
        "//////////////////////8AAAAATGF2YzYwLjMxAAAAAAAAAAAAAAAAJAZgAAAAAAAAA8B1vxhPAAAA"
        "AAAAAAAAAAAAAAAA//tUxAAACVxHMjWWAAFUmKbDOQAAH4uWWzLTlpy06ANt30dxc4FJNV857TvtOmE2"
        "TwEeleAwSAaAQAgDQSCYeL152Zma9e+DgIAgCDoPg/yG7l/Ocu/SGOXfwxy/u6fcBQgYVD25n6EySVgL"
        "w0Yir4UCZY8ZqMuGQxMVlPRWU/AWwBlXBsKI6D5vw+IckXL+K1IcOcOcTP/kVIqZF4mjH/8ul1IvF5FH"
        "//MVA0JQl/g0JToNFflUuphEADmAGAAp//tUxAWDyZgrFB33gACqraBBcB+AgbwCOYGQB9mBSAC5gHAJ"
        "KYUIGpmFKI+5i44bcYUCCAGADAFZgDwCOYCMACmADgAQGUCsPJ///5L/7f///t/+z/5H4RAKwYGJeEen"
        "UGGWf/3/9b///+WS1/idv///X///4nb///yj///+Cgf9agQA/kjSIAa/D9akaQqUWBUMBYDAwMAHjA9B"
        "nMKsMMwb1YTBPDkBQfhgKgFmAGAWCgE5OIRnnYOmqdQNFdB9//tUxB8AEUC5J1XsABFUj+u3J4ACItTd"
        "TRrk40tIdItY7D0hy2iKEIdNd7X5fE2HoBE6IQ+7lw/lVhtrjuXqeX0+HJW5Es5nn3XK8YpOZ5/hzOks"
        "Y7z/DmdJYAZ+JDmsEJAAABYLRsNhqGAwAAAARhAbuLOHTjmBYycVi2ET8jcJgfWc/wU9UaXEflNj/8DL"
        "VjRwkspsf/optfVwsd/6Glwu1f//9rjO4bdxy4mtq+CYkC7P1qbVTEFNRTMuMTAw//tUxASDwAABpBwA"
        "ACAAADSAAAAEVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
        "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
        "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"};

    auto decode = [](char character) -> int {
        if (character >= 'A' && character <= 'Z') {
            return character - 'A';
        }
        if (character >= 'a' && character <= 'z') {
            return character - 'a' + 26;
        }
        if (character >= '0' && character <= '9') {
            return character - '0' + 52;
        }
        return character == '+' ? 62 : character == '/' ? 63 : -1;
    };

    std::array<std::byte, 960> decoded{};
    std::size_t decoded_size = 0;
    uint32_t accumulator = 0;
    unsigned bit_count = 0;
    for (const char character : encoded) {
        const int value = decode(character);
        if (value < 0) {
            return false;
        }
        accumulator = (accumulator << 6U) | static_cast<uint32_t>(value);
        bit_count += 6;
        if (bit_count >= 8) {
            bit_count -= 8;
            decoded[decoded_size++] = static_cast<std::byte>(
                (accumulator >> bit_count) & 0xffU);
        }
    }
    if (decoded_size != decoded.size()) {
        return false;
    }

    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    for (std::size_t repetition = 0;
         repetition < audio_frame_repetitions;
         ++repetition) {
        stream.write(
            reinterpret_cast<const char*>(decoded.data() + encoded_frame_bytes),
            decoded.size() - encoded_frame_bytes);
    }
    return stream.good();
}