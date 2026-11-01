#include <musicrat/backends/media/directory_browser.hpp>

#include "../support/directory_fixture.hpp"

#include <cassert>
#include <fstream>

int main() {
    namespace fs = std::filesystem;
    using namespace CommRaT::Messages;
    musicrat::testing::TestDirectory fixture;
    const auto root = fixture.path / "media";
    fs::create_directories(root / "album");
    const auto track = root / "album" / "track.wav";
    musicrat::testing::make_file(track);
    musicrat::testing::make_file(root / "unlisted.wav");
    musicrat::backends::media::DirectoryBrowser browser;
    assert(browser.configure(root) == MEDIA_BROWSE_ERROR_NONE);
    MediaDirectoryPage page{};
    browser.browse({.request_id = 1, .directory = "album"}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE);
    const auto snapshot = page.snapshot_id;
    MediaSelectionResult result{};
    MediaSelectionRequest request{.request_id = 2, .snapshot_id = snapshot, .relative_path = "album/track.wav"};
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_NONE && result.request_id == 2);
    assert(result.snapshot_id == snapshot && result.entry.name == "track.wav");
    assert(result.resolved_path == track.c_str());
    browser.read_page({.request_id = 3, .snapshot_id = snapshot}, page);
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_NONE);

    auto invalid = request;
    invalid.snapshot_id = 0;
    browser.resolve_selection(invalid, result);
    assert(result.error == MEDIA_BROWSE_ERROR_STALE_SNAPSHOT && result.resolved_path.empty());
    invalid = request;
    invalid.request_id = 0;
    browser.resolve_selection(invalid, result);
    assert(result.error == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
    for (const char* path : {"unlisted.wav", "../secret.wav", "/tmp/file", "album/./track.wav"}) {
        invalid = request;
        invalid.relative_path = path;
        browser.resolve_selection(invalid, result);
        assert(result.error == MEDIA_BROWSE_ERROR_INVALID_REQUEST && result.resolved_path.empty());
    }
    invalid = request;
    invalid.relative_path = std::string_view{"album/track.wav\0suffix", 22};
    browser.resolve_selection(invalid, result);
    assert(result.error == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
    {
        std::ofstream changed{track, std::ios::app};
        changed << "modified";
    }
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_SELECTION_CHANGED && result.resolved_path.empty());
    browser.browse({.request_id = 4, .directory = "album"}, page);
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_STALE_SNAPSHOT);
    request.snapshot_id = page.snapshot_id;
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_NONE);

    const auto saved = root / "saved";
    fs::rename(track, saved);
    musicrat::testing::make_file(track);
    fs::resize_file(track, fs::file_size(saved));
    fs::last_write_time(track, fs::last_write_time(saved));
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_SELECTION_CHANGED);
    fs::remove(track);
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_NOT_FOUND);
    fs::create_symlink(saved, track);
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_OUTSIDE_ROOT);
    fs::remove(track);
    fs::create_directory(track);
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_NOT_FILE);
    {
        const auto prefix = fixture.path / std::string(200, 'a') / std::string(200, 'b');
        const auto padding = 512 - prefix.string().size() - 1 - std::string_view{"/track.wav"}.size();
        const auto long_root = prefix / std::string(padding, 'c');
        fs::create_directories(long_root);
        musicrat::testing::make_file(long_root / "track.wav");
        musicrat::backends::media::DirectoryBrowser long_browser;
        assert(long_browser.configure(long_root) == MEDIA_BROWSE_ERROR_NONE);
        MediaDirectoryPage long_page{};
        long_browser.browse({.request_id = 1}, long_page);
        long_browser.resolve_selection({
            .request_id = 2, .snapshot_id = long_page.snapshot_id, .relative_path = "track.wav",
        }, result);
        assert(result.error == MEDIA_BROWSE_ERROR_NONE && result.resolved_path.size() == 512);
        musicrat::testing::make_file(long_root / "tracks.wav");
        long_browser.browse({.request_id = 3}, long_page);
        long_browser.resolve_selection({
            .request_id = 4, .snapshot_id = long_page.snapshot_id, .relative_path = "tracks.wav",
        }, result);
        assert(result.error == MEDIA_BROWSE_ERROR_PATH_TOO_LONG && result.resolved_path.empty());
    }
    fs::remove(track);
    fs::rename(saved, track);
    browser.browse({.request_id = 5, .directory = "album"}, page);
    request.snapshot_id = page.snapshot_id;
    fs::rename(root / "album", root / "moved");
    fs::create_directory_symlink(root / "moved", root / "album");
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_OUTSIDE_ROOT);
    fs::remove(root / "album");
    fs::rename(root / "moved", root / "album");

    browser.browse({.request_id = 6}, page);
    browser.resolve_selection({
        .request_id = 7, .snapshot_id = page.snapshot_id, .relative_path = "album",
    }, result);
    assert(result.error == MEDIA_BROWSE_ERROR_NOT_FILE);
    browser.browse({.request_id = 8, .directory = "album"}, page);
    request.snapshot_id = page.snapshot_id;
    fs::rename(root, fixture.path / "old-root");
    fs::create_directory(root);
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_SELECTION_CHANGED && result.resolved_path.empty());
    assert(browser.configure(fixture.path / "old-root") == MEDIA_BROWSE_ERROR_NONE);
    browser.resolve_selection(request, result);
    assert(result.error == MEDIA_BROWSE_ERROR_STALE_SNAPSHOT);
}
