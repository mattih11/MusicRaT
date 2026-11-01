#include <musicrat/backends/media/directory_browser_worker.hpp>

#include <utility>

namespace musicrat::backends::media {

DirectoryBrowserWorker::DirectoryBrowserWorker(std::filesystem::path root, uint32_t scan_limit)
    : root_(std::move(root)), scan_limit_(scan_limit),
      worker_([this](std::stop_token stop) { run(stop); }) {}

DirectoryBrowserWorker::~DirectoryBrowserWorker() {
    worker_.request_stop();
    wake();
    worker_.join();
}

void DirectoryBrowserWorker::wake() noexcept {
    wake_sequence_.fetch_add(1, std::memory_order_release);
    wake_sequence_.notify_one();
}

DirectoryBrowserWorker::Submission DirectoryBrowserWorker::submit(
    const CommRaT::Messages::MediaDirectoryQuery& query) noexcept {
    if (state_.load(std::memory_order_acquire) != State::Idle) return Submission::Busy;
    query_ = query;
    kind_ = RequestKind::Browse;
    state_.store(State::Queued, std::memory_order_release);
    wake();
    return Submission::Accepted;
}

DirectoryBrowserWorker::Submission DirectoryBrowserWorker::submit(
    const CommRaT::Messages::MediaDirectoryPageRequest& request) noexcept {
    if (state_.load(std::memory_order_acquire) != State::Idle) return Submission::Busy;
    page_request_ = request;
    kind_ = RequestKind::Page;
    state_.store(State::Queued, std::memory_order_release);
    wake();
    return Submission::Accepted;
}

bool DirectoryBrowserWorker::poll(CommRaT::Messages::MediaDirectoryPage& output) noexcept {
    if (state_.load(std::memory_order_acquire) != State::Ready) return false;
    if (kind_ == RequestKind::Selection) return false;
    output = result_;
    state_.store(State::Idle, std::memory_order_release);
    return true;
}

DirectoryBrowserWorker::Submission DirectoryBrowserWorker::submit(
    const CommRaT::Messages::MediaSelectionRequest& request) noexcept {
    if (state_.load(std::memory_order_acquire) != State::Idle) return Submission::Busy;
    selection_request_ = request;
    kind_ = RequestKind::Selection;
    state_.store(State::Queued, std::memory_order_release);
    wake();
    return Submission::Accepted;
}

bool DirectoryBrowserWorker::poll(CommRaT::Messages::MediaSelectionResult& output) noexcept {
    if (state_.load(std::memory_order_acquire) != State::Ready) return false;
    if (kind_ != RequestKind::Selection) return false;
    output = selection_result_;
    state_.store(State::Idle, std::memory_order_release);
    return true;
}

void DirectoryBrowserWorker::run(std::stop_token stop) {
    const auto configuration_error = browser_.configure(root_, scan_limit_);
    for (;;) {
        const auto observed = wake_sequence_.load(std::memory_order_acquire);
        if (stop.stop_requested()) return;
        if (state_.load(std::memory_order_acquire) != State::Queued) {
            wake_sequence_.wait(observed, std::memory_order_acquire);
            continue;
        }
        state_.store(State::Working, std::memory_order_relaxed);
        if (kind_ == RequestKind::Selection) {
            if (configuration_error != CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE) {
                selection_result_ = {
                    .request_id = selection_request_.request_id,
                    .snapshot_id = selection_request_.snapshot_id,
                    .error = configuration_error,
                };
            } else {
                browser_.resolve_selection(selection_request_, selection_result_);
            }
        } else if (configuration_error != CommRaT::Messages::MEDIA_BROWSE_ERROR_NONE) {
            result_ = {};
            result_.request_id = kind_ == RequestKind::Page ? page_request_.request_id : query_.request_id;
            result_.error = configuration_error;
        } else if (kind_ == RequestKind::Page) {
            browser_.read_page(page_request_, result_);
        } else {
            browser_.browse(query_, result_);
        }
        state_.store(State::Ready, std::memory_order_release);
    }
}

} // namespace musicrat::backends::media
