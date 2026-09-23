#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

inline bool write_opus_fixture(const std::filesystem::path& path) {
    constexpr std::string_view encoded{
        "T2dnUwACAAAAAAAAAAAauN+xAAAAAEBMSqQBE09wdXNIZWFkAQE4AYC7AAAAAABPZ2dTAAAAAAAAAAAAABq437EBAAAAE+NDXgE+"
        "T3B1c1RhZ3MNAAAATGF2ZjYwLjE2LjEwMAEAAAAdAAAAZW5jb2Rlcj1MYXZjNjAuMzEuMTAyIGxpYm9wdXNPZ2dTAACAuwAAAAAA"
        "ABq437ECAAAAa3HsiTIeHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHkiDm6TZP/vovNoK"
        "Iea2bwZEjOeu+ikNpeLuoCJ2NUtBBqXXgo6cBtFn5ZeN+IamFb7ectfC4AAAAAAAAEtBAqRTeZciUz2Aus2FMV/BJBASZPsUmb49"
        "k80AAEtBAad3hHJhzT1UKhQotAcdt5H1iGfeNRrQm07wAEsBootIzZr0H2JndTXnvr5DJjvB5VulB5MmTtdkiUtBAp36dcT6ME2j"
        "Cso/0Q2v4DFtn8q+iwn7GBAAAEtBAKUcCM2a9B9iZ3SGKPQkCsnWpK8Cn+1XqPu+WEtBAqCH6QwlSHses427e+N0QMmWzgY4NL+3"
        "lDcAAEtBAqejBY1K30LGfk+NbrWjk1+GOeV2PUaFntgAAEtBAaCFWEwlSHses428ncU2F3fCh/bN08+Q+EzAAEtBBKZkaM2a9B9i"
        "Z4eXEzLVRSsB6jI+H2xIAAAAAEsBoc041vEG0Wfll434l14+oEnY4OLndg7DuKmM2EtBAqNNLw0ldayV331WNahnBmjTqH+c7jc4"
        "24AAAEtBAaMTh4wlSHses429wdm5Emx2c3jxn+5t/leAAEsBo9OozZr0H2Jngi8qRgCG7ykloPZNInirNQy1MktBA6YYBApH92fz"
        "TxatQXFQp7EjYxVoExER0AAAAEtBBKHL0Gf1cAL4ETIybUjWOtQkY5GxjUy1AAAAAEtBBqH+fXvQPSRWN7ELgThDONWhHjjK/AAA"
        "AAAAAEtBBKjL907N77fS6fuvnpAoJep1+BU4shoQAAAAAEsBqNCNZjTfQsZ+TyhOqUyNSS6822fqweV+6WmGQEtBAZ9B03XI/SRW"
        "N7EK75DA7r1YcqgofmVO9PrAAEiq2JRtW5p4duhBBuERaZFAkyiRrJGULLiL5c2SIEtBALSvAHOemnurqXAwYdKuhTO06LTHXoPS"
        "wkkYgEtBAaMYKZbyAJePcNMoumuaw+VhsSymR8JVGFXwAEtBAqbjOq3kARYTRYHKNNe999s7KIF1ssT730gAAEilmtpE/gFzduk8"
        "wrZnl28G8hYRPsbS14cQituR2kiii0nBGNCOB0Pg4HrXfM7+N2N9r7iO+Wygocsp/UtBAKJUi05LhuGNMwv9EVlcJQl9NXMF1lkK"
        "XP52SkikLbX6Ne/igK/X3PreVbSrIRB+CZ16tHL21oPvhEihQunBGNCOB0PgQNjaG02T/USoRS7LudWGDtf2IEihQunBGNCOB0Pf"
        "rvwEIFZ98IwOzmn087P/LyeE0EihQunBGNCOB0PgRQql5Fd0z+02oimrbKaSWea5sEihoGoDEdX44Ic0raJODJXfZX2G2yo941Z0"
        "HddJkEihQunBGNCOB0Pfp5Nr/IGKpTTuc+9mPWvhQQn1gEihqR+WnXQsS67MFNQCLA8o71eOp8EoCWY4D8dPgEtBATte+DyhTxmM"
        "UDQr9FbNnI3G7rTGWs9mKJK8AEg6jog8oVA7PTmndUwwdgWZT9E2Cbjz3ezb8w3/gEtBATte+DyhVf3WZY3SrukoN593hqnEJBPk"
        "JpqAAEtBATte+DyhTxmyjoFZlaUIeRcNcFIePSAq4qvgAEsBO174PKFJTY6uPa3zn17rLvoWjeTmO1OadjqB/0g6jog8oUmzxqna"
        "okv/OB1NZiPs7ZwWmA0fa/IL2EtBADqOiDyhVzXRTkUqAFX7h7jlaV953+4nBMVIQ0tBATwvaDyhTrID2Fg9n9KT1d8btLUB3c2P"
        "7zhTAEsBO174PKFXKdLKVJ9Rns59E5HdlJJficIy3eUTtEtBATte+DyhVgJ95P67RgW92uj4c3wBOLf2pOYHAEtBADte+DyhSFXh"
        "7mo10Wv4h5UakiA/Z22uTTVLqEg6jog8oVcqNfS3j9cNkpUxMa0BTA+Qr2YGrYEzIEg7Xvg8oVANmPXIbSegS+r4JVveWTh72+8A"
        "h9s7gEg6jog8oVdxYb5qsfZ1LkvmVxQ2lsCyffGaPyHHUEg6jog8oVdBPpp2A9WAwPnXeYQi3ZOYB5GkHM4ZcE9nZ1MABLi8AAAA"
        "AAAAGrjfsQMAAACE4nTQAR5LQQAGAxJnGDKEidgMWlhiy2afSUbqHtLPPPojPTA="};

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

    std::array<std::byte, 1772> decoded{};
    std::size_t decoded_size = 0;
    uint32_t accumulator = 0;
    unsigned bit_count = 0;
    for (const char character : encoded) {
        if (character == '=') {
            break;
        }
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
    stream.write(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    return stream.good();
}