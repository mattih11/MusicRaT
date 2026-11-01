#include <musicrat/backends/media/directory_browser_worker.hpp>

#include "../support/directory_fixture.hpp"

#include <cassert>
#include <chrono>
#include <thread>

namespace {

using namespace CommRaT::Messages;
using musicrat::backends::media::DirectoryBrowserWorker;

template<typename Result>
void await(DirectoryBrowserWorker& worker, Result& page) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!worker.poll(page)) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
}

} // namespace

int main() {
    using Submission = DirectoryBrowserWorker::Submission;
    musicrat::testing::TestDirectory fixture;
    std::filesystem::create_directory(fixture.path / "album");
    for (uint32_t index = 0; index < 19; ++index) {
        musicrat::testing::make_file(fixture.path / ("track-" + std::to_string(index)));
    }
    MediaDirectoryPage page{};
    {
        DirectoryBrowserWorker worker{fixture.path};
        assert(!worker.poll(page));
        assert(worker.submit(MediaDirectoryQuery{.request_id = 1, .page_size = 8})
            == Submission::Accepted);
        // The outstanding slot stays occupied even after the result is ready.
        assert(worker.submit(MediaDirectoryQuery{.request_id = 2}) == Submission::Busy);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!worker.has_result()) {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        assert(worker.submit(MediaDirectoryQuery{.request_id = 2}) == Submission::Busy);
        MediaSelectionResult untouched{};
        assert(!worker.poll(untouched) && worker.has_result());
        await(worker, page);
        assert(page.request_id == 1 && page.error == MEDIA_BROWSE_ERROR_NONE);
        assert(page.total_entries == 20 && page.entries.size() == 8);
        assert(page.entries[0].name == "album");
        const auto snapshot = page.snapshot_id;
        musicrat::testing::make_file(fixture.path / "new-track");
        assert(worker.submit(MediaDirectoryPageRequest{
            .request_id = 3, .snapshot_id = snapshot, .offset = 8, .page_size = 8,
        }) == Submission::Accepted);
        await(worker, page);
        assert(page.request_id == 3 && page.total_entries == 20 && page.offset == 8);
        assert(page.entries.size() == 8 && page.has_more);
        MediaSelectionResult selection{};
        assert(!worker.poll(selection));
        assert(worker.submit(MediaSelectionRequest{
            .request_id = 111, .snapshot_id = snapshot, .relative_path = "track-0",
        }) == Submission::Accepted);
        const auto selection_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!worker.has_result()) {
            assert(std::chrono::steady_clock::now() < selection_deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        assert(!worker.poll(page));
        assert(worker.has_result());
        assert(worker.submit(MediaDirectoryQuery{.request_id = 112}) == Submission::Busy);
        await(worker, selection);
        assert(selection.error == MEDIA_BROWSE_ERROR_NONE && selection.request_id == 111);
        assert(selection.snapshot_id == snapshot && selection.entry.name == "track-0");
        std::filesystem::remove(fixture.path / "track-0");
        assert(worker.submit(MediaSelectionRequest{
            .request_id = 112, .snapshot_id = snapshot, .relative_path = "track-0",
        }) == Submission::Accepted);
        await(worker, selection);
        assert(selection.error == MEDIA_BROWSE_ERROR_NOT_FOUND && selection.resolved_path.empty());
        assert(worker.submit(MediaDirectoryQuery{.request_id = 4, .directory = "album"})
            == Submission::Accepted);
        await(worker, page);
        assert(page.directory == "album" && page.entries.empty());
        assert(worker.submit(MediaDirectoryPageRequest{
            .request_id = 5, .snapshot_id = snapshot,
        }) == Submission::Accepted);
        await(worker, page);
        assert(page.error == MEDIA_BROWSE_ERROR_STALE_SNAPSHOT && page.request_id == 5);
        assert(worker.submit(MediaDirectoryQuery{.request_id = 6, .directory = "../"})
            == Submission::Accepted);
        await(worker, page);
        assert(page.error == MEDIA_BROWSE_ERROR_OUTSIDE_ROOT);
        assert(worker.submit(MediaDirectoryQuery{.request_id = 0}) == Submission::Accepted);
        await(worker, page);
        assert(page.error == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
        for (uint64_t id = 7; id < 107; ++id) {
            assert(worker.submit(MediaDirectoryQuery{.request_id = id}) == Submission::Accepted);
            await(worker, page);
            assert(page.request_id == id && page.error == MEDIA_BROWSE_ERROR_NONE);
        }
    }
    {
        DirectoryBrowserWorker worker{fixture.path / "missing"};
        assert(worker.submit(MediaDirectoryQuery{.request_id = 108}) == Submission::Accepted);
        await(worker, page);
        assert(page.error == MEDIA_BROWSE_ERROR_NOT_FOUND && page.request_id == 108);
        assert(worker.submit(MediaSelectionRequest{
            .request_id = 113, .snapshot_id = 1, .relative_path = "track",
        }) == Submission::Accepted);
        MediaSelectionResult selection{};
        await(worker, selection);
        assert(selection.error == MEDIA_BROWSE_ERROR_NOT_FOUND && selection.request_id == 113);
    }
    {
        DirectoryBrowserWorker worker{fixture.path, 1};
        assert(worker.submit(MediaDirectoryQuery{.request_id = 109}) == Submission::Accepted);
        await(worker, page);
        assert(page.error == MEDIA_BROWSE_ERROR_SCAN_LIMIT);
    }
    {
        DirectoryBrowserWorker worker{fixture.path};
        assert(worker.submit(MediaDirectoryQuery{.request_id = 110}) == Submission::Accepted);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!worker.has_result()) {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    // Shutdown must wake an idle worker and must not wait for a client to drain a result.
    for (uint32_t attempt = 0; attempt < 20; ++attempt) {
        DirectoryBrowserWorker idle{fixture.path};
        DirectoryBrowserWorker queued{fixture.path};
        assert(queued.submit(MediaDirectoryQuery{.request_id = 110}) == Submission::Accepted);
    }
}
