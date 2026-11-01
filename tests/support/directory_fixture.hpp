#pragma once

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace musicrat::testing {

struct TestDirectory {
    std::filesystem::path path;

    TestDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "musicrat-browser-XXXXXX").string();
        const auto* created = ::mkdtemp(pattern.data());
        assert(created != nullptr);
        path = created;
    }

    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;

    ~TestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        assert(!error);
    }
};

inline void make_file(const std::filesystem::path& path) {
    std::ofstream file{path};
    file << "not a decoded audio track";
    assert(file.good());
}

} // namespace musicrat::testing
