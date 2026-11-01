#pragma once

#include <musicrat/backends/media/directory_browser.hpp>

#include <atomic>
#include <filesystem>
#include <thread>

namespace musicrat::backends::media {

// One ordinary-thread client submits/polls; only the worker touches the filesystem.
class DirectoryBrowserWorker {
public:
    enum class Submission { Accepted, Busy };

    explicit DirectoryBrowserWorker(std::filesystem::path root, uint32_t scan_limit = 4096);
    ~DirectoryBrowserWorker();

    DirectoryBrowserWorker(const DirectoryBrowserWorker&) = delete;
    DirectoryBrowserWorker& operator=(const DirectoryBrowserWorker&) = delete;

    [[nodiscard]] Submission submit(const CommRaT::Messages::MediaDirectoryQuery& query) noexcept;
    [[nodiscard]] Submission submit(const CommRaT::Messages::MediaDirectoryPageRequest& request) noexcept;
    [[nodiscard]] Submission submit(const CommRaT::Messages::MediaSelectionRequest& request) noexcept;
    [[nodiscard]] bool has_result() const noexcept {
        return state_.load(std::memory_order_acquire) == State::Ready;
    }
    [[nodiscard]] bool poll(CommRaT::Messages::MediaDirectoryPage& output) noexcept;
    [[nodiscard]] bool poll(CommRaT::Messages::MediaSelectionResult& output) noexcept;

private:
    enum class State { Idle, Queued, Working, Ready };
    enum class RequestKind { Browse, Page, Selection };

    void run(std::stop_token stop);
    void wake() noexcept;

    std::filesystem::path root_;
    uint32_t scan_limit_;
    DirectoryBrowser browser_{};
    CommRaT::Messages::MediaDirectoryQuery query_{};
    CommRaT::Messages::MediaDirectoryPageRequest page_request_{};
    CommRaT::Messages::MediaDirectoryPage result_{};
    CommRaT::Messages::MediaSelectionRequest selection_request_{};
    CommRaT::Messages::MediaSelectionResult selection_result_{};
    RequestKind kind_{RequestKind::Browse};
    std::atomic<State> state_{State::Idle};
    std::atomic<uint64_t> wake_sequence_{0};
    std::jthread worker_;
};

} // namespace musicrat::backends::media
