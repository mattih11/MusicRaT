#pragma once

#include <charconv>
#include <initializer_list>
#include <cstdint>
#include <string_view>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace musicrat::backends::audio::detail {

struct ThreadSchedulingCounters {
    uint64_t runtime_ns{0};
    uint64_t runnable_delay_ns{0};
    uint64_t timeslices{0};
};

[[nodiscard]] inline bool parse_thread_scheduling(
    std::string_view text, ThreadSchedulingCounters& counters) noexcept {
    ThreadSchedulingCounters parsed{};
    for (uint64_t* field : {
             &parsed.runtime_ns, &parsed.runnable_delay_ns, &parsed.timeslices}) {
        const auto first = text.find_first_not_of(" \t\n");
        if (first == std::string_view::npos) {
            return false;
        }
        text.remove_prefix(first);
        const auto result = std::from_chars(
            text.data(), text.data() + text.size(), *field);
        if (result.ec != std::errc{}
            || (result.ptr != text.data() + text.size()
                && *result.ptr != ' ' && *result.ptr != '\t'
                && *result.ptr != '\n')) {
            return false;
        }
        text.remove_prefix(static_cast<std::size_t>(result.ptr - text.data()));
    }
    if (text.find_first_not_of(" \t\n") != std::string_view::npos) {
        return false;
    }
    counters = parsed;
    return true;
}

class WaitSchedulingObserver {
public:
    ~WaitSchedulingObserver() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }

    WaitSchedulingObserver() = default;
    WaitSchedulingObserver(const WaitSchedulingObserver&) = delete;
    WaitSchedulingObserver& operator=(const WaitSchedulingObserver&) = delete;

    [[nodiscard]] int open() noexcept {
        descriptor_ = ::open("/proc/thread-self/schedstat", O_RDONLY | O_CLOEXEC);
        return descriptor_ < 0 ? -errno : 0;
    }

    [[nodiscard]] int read(ThreadSchedulingCounters& counters) const noexcept {
        char buffer[128]{};
        const auto size = ::pread(descriptor_, buffer, sizeof(buffer), 0);
        if (size < 0) {
            return -errno;
        }
        if (static_cast<std::size_t>(size) == sizeof(buffer)
            || !parse_thread_scheduling(
                std::string_view{buffer, static_cast<std::size_t>(size)}, counters)) {
            return -EINVAL;
        }
        return 0;
    }

private:
    int descriptor_{-1};
};

} // namespace musicrat::backends::audio::detail
