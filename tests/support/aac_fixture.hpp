#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

inline bool write_aac_fixture(const std::filesystem::path& path) {
    constexpr std::size_t segment_repetitions = 30;
    constexpr std::string_view encoded{
        "//FMQBMf/N4CAExhdmM2MC4zMS4xMDIAAlyjUqwwNE6aw6Qgnt/q6lVy44ZJJJD4i0//W5J/+Anp2enf"
        "63+UIEIE0ZZKWTVtIqqGkv9HvJx+G9xSq2lNaZKRKBgYGBgYGBgYGBgZEDAxs2bNgyJFLLLLLdsCzoF2"
        "vs6Bdr7OgWa+zn2K+znVK6zm1K6xm1K2pmxRRRRRRRf/8UxAFR/8ARqU2spdlkurJdWS6f/3f+/11p5a"
        "vW//7H/X79cca1r+n/9j/r9+uONa18//1f9vv1x1rXAjLtHNoJCQlpVx3AZQDGXupMUQNEpuJX0iKcfR"
        "uIpx9PpERx9PpFEJwPp9PoUQAHP9Poq47gMoBjL3UmKIGiU3Er6RFOPo3EU4+n0iI4+n0iiE4H0+n0KI"
        "ADn+n0vGxykUUURRRRRRRRfKKLj/8UxADB/8ASjxizoRHoRDohDoXNL5533n/H/vr2nDVy5cjr1Jxq5l"
        "hLvvoJ/4+MjiB8fHwBn9yD/wA77wg/wYO+5B/4Ad9wH/gB33AMxfxJfwMXhMuAphPN/AxeEDQcD/8UxA"
        "Cv/8APgxEvpRDohCARCplVm8/+n/x//4/m5dy+fNx4evqT+918eADAwMOHAwMDA1sDrcGBgYGBswYkDA"
        "wMSzBgYk7rBiV4GBgYGBgYGJLAiABA7/8UxAB//8AToxivrFCqx4/v/386kuXJI64tJJEF2/wk6tvZhj"
        "X5i4eCY9vYco/BJrqsCj8JDXdhyjvSa6o/D/8UxACN/8AUgxivpSDphSmZ47/XjVyS5JHXJJ7SSQM77e"
        "jI7zZs5/g3uduHI6WbO+aHIkm875oXHSbzvmhcdJt07bT4Lg"};

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

    std::array<std::byte, 636> decoded{};
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
         repetition < segment_repetitions;
         ++repetition) {
        stream.write(
            reinterpret_cast<const char*>(decoded.data()),
            decoded.size());
    }
    return stream.good();
}