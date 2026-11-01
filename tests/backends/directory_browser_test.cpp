#include <musicrat/backends/media/directory_browser.hpp>

#include "../support/directory_fixture.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;
using namespace CommRaT::Messages;

using musicrat::testing::TestDirectory;
using musicrat::testing::make_file;

} // namespace

int main() {
    using musicrat::backends::media::DirectoryBrowser;
    TestDirectory fixture;
    const auto root = fixture.path / "media";
    const auto outside = fixture.path / "outside";
    fs::create_directories(root / "album");
    fs::create_directories(root / "z-folder");
    fs::create_directory(outside);
    make_file(root / "A.wav");
    make_file(root / "z.mp3");
    make_file(root / ".hidden");
    make_file(root / "unsupported.txt");
    make_file(root / "album" / "track.flac");
    make_file(outside / "private.wav");
    fs::create_directory_symlink(outside, root / "outside-link");
    fs::create_directory_symlink(root / "album", root / "inside-link");
    fs::create_symlink(root / "missing", root / "broken-link");

    DirectoryBrowser browser;
    MediaDirectoryPage page{};
    browser.browse({.request_id = 1}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NOT_CONFIGURED);
    assert(browser.configure(root) == MEDIA_BROWSE_ERROR_NONE);
    browser.browse({.request_id = 2, .page_size = 2}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE);
    assert(page.request_id == 2 && page.snapshot_id != 0);
    assert(page.directory == ".");
    assert(page.total_entries == 6 && page.skipped_entries == 3);
    assert(page.entries.size() == 2);
    assert(page.entries[0].name == "album");
    assert(page.entries[0].kind == MEDIA_DIRECTORY_ENTRY_DIRECTORY);
    assert(page.entries[1].name == "z-folder");
    assert(page.offset == 0 && page.next_offset == 2 && page.has_more);
    const auto snapshot = page.snapshot_id;
    browser.read_page({
        .request_id = 3, .snapshot_id = snapshot, .offset = 2, .page_size = 2,
    }, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE && page.request_id == 3);
    assert(page.entries[0].name == ".hidden");
    assert(page.entries[1].name == "A.wav");
    assert(page.next_offset == 4 && page.has_more);
    make_file(root / "new.wav");
    browser.read_page({
        .request_id = 4, .snapshot_id = snapshot, .offset = 4,
    }, page);
    assert(page.entries.size() == 2 && page.total_entries == 6);
    assert(page.entries[0].name == "unsupported.txt");
    assert(page.entries[1].name == "z.mp3");
    assert(!page.has_more && page.next_offset == 6);
    browser.read_page({
        .request_id = 5, .snapshot_id = snapshot, .offset = 6,
    }, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE && page.entries.empty());
    browser.read_page({
        .request_id = 6, .snapshot_id = snapshot, .offset = 7,
    }, page);
    assert(page.error == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
    assert(page.entries.empty() && page.snapshot_id == 0 && !page.has_more);

    browser.browse({.request_id = 7, .directory = "album"}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE);
    assert(page.snapshot_id != snapshot);
    assert(page.entries.size() == 1 && page.entries[0].relative_path == "album/track.flac");
    browser.read_page({.request_id = 8, .snapshot_id = snapshot}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_STALE_SNAPSHOT);

    for (const char* path : {"../outside", "/tmp", "album/../../outside", "outside-link", "inside-link"}) {
        browser.browse({.request_id = 9, .directory = path}, page);
        assert(page.error == MEDIA_BROWSE_ERROR_OUTSIDE_ROOT);
        assert(page.entries.empty() && page.snapshot_id == 0 && !page.has_more);
    }
    browser.browse({.request_id = 10, .directory = "missing"}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NOT_FOUND);
    browser.browse({.request_id = 11, .directory = "A.wav"}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NOT_DIRECTORY);
    browser.browse({.request_id = 12, .directory = "album/.."}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE && page.total_entries == 7);
    MediaDirectoryQuery embedded_null{.request_id = 13};
    embedded_null.directory = std::string_view{"album\0/track.flac", 17};
    browser.browse(embedded_null, page);
    assert(page.error == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
    browser.browse({.page_size = 1}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
    browser.browse({.request_id = 14, .page_size = 0}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
    browser.browse({.request_id = 15, .page_size = 33}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_INVALID_REQUEST);

    assert(browser.configure(root, 2) == MEDIA_BROWSE_ERROR_NONE);
    browser.browse({.request_id = 16}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_SCAN_LIMIT && page.entries.empty());
    assert(browser.configure(root / "A.wav") == MEDIA_BROWSE_ERROR_NOT_DIRECTORY);
    browser.browse({.request_id = 17}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NOT_CONFIGURED);
    assert(browser.configure(root, 0) == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
    assert(browser.configure(root, 65537) == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
    assert(browser.configure(root / "missing") == MEDIA_BROWSE_ERROR_NOT_FOUND);
    assert(browser.configure({}) == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
    auto invalid_root = root.string();
    invalid_root += '\0';
    invalid_root += "suffix";
    assert(browser.configure(fs::path{invalid_root}) == MEDIA_BROWSE_ERROR_INVALID_REQUEST);

    fs::create_directory(root / "empty");
    assert(browser.configure(root, 1) == MEDIA_BROWSE_ERROR_NONE);
    browser.browse({.request_id = 17, .directory = "empty"}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE && page.entries.empty());
    assert(!page.has_more && page.total_entries == 0 && page.next_offset == 0);
    const auto empty_snapshot = page.snapshot_id;
    browser.read_page({.request_id = 17, .snapshot_id = empty_snapshot, .page_size = 0}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_INVALID_REQUEST);
    browser.read_page({.request_id = 17, .snapshot_id = empty_snapshot}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE);

    if (::geteuid() != 0) {
        fs::permissions(root / "empty", fs::perms::none);
        browser.browse({.request_id = 17, .directory = "empty"}, page);
        fs::permissions(root / "empty", fs::perms::owner_all);
        assert(page.error == MEDIA_BROWSE_ERROR_PERMISSION_DENIED);
    }

    const auto maximum_name = root / std::string(255, 'm');
    make_file(maximum_name);
    assert(browser.configure(root) == MEDIA_BROWSE_ERROR_NONE);
    browser.browse({.request_id = 17}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE);
    assert(std::any_of(page.entries.begin(), page.entries.end(), [](const auto& entry) {
        return entry.name.size() == 255;
    }));

    const auto large = root / "large";
    fs::create_directory(large);
    for (uint32_t index = 0; index < 70; ++index) {
        make_file(large / ("track-" + std::to_string(index) + ".wav"));
    }
    assert(browser.configure(root) == MEDIA_BROWSE_ERROR_NONE);
    browser.browse({.request_id = 18, .directory = "large"}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_NONE);
    assert(page.total_entries == 70 && page.entries.size() == MediaDirectoryPage::MAX_ENTRIES);
    const auto large_snapshot = page.snapshot_id;
    uint32_t count = 0;
    std::string previous;
    do {
        for (const auto& entry : page.entries) {
            assert(previous.empty() || previous < entry.name.c_str());
            previous = entry.name.c_str();
            ++count;
        }
        if (!page.has_more) break;
        browser.read_page({
            .request_id = 19, .snapshot_id = large_snapshot, .offset = page.next_offset,
        }, page);
        assert(page.error == MEDIA_BROWSE_ERROR_NONE);
    } while (true);
    assert(count == 70);
    assert(browser.configure(root) == MEDIA_BROWSE_ERROR_NONE);
    browser.read_page({.request_id = 20, .snapshot_id = large_snapshot}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_STALE_SNAPSHOT);

    const std::string component(200, 'a');
    const auto deep = root / component / component;
    fs::create_directories(deep);
    make_file(deep / std::string(200, 'b'));
    const auto relative = deep.lexically_relative(root).generic_string();
    MediaDirectoryQuery too_long{.request_id = 21};
    too_long.directory = relative.c_str();
    browser.browse(too_long, page);
    assert(page.error == MEDIA_BROWSE_ERROR_PATH_TOO_LONG && page.entries.empty());

    fs::rename(root, fixture.path / "removed-media");
    fs::create_directory_symlink(outside, root);
    browser.browse({.request_id = 22}, page);
    assert(page.error == MEDIA_BROWSE_ERROR_OUTSIDE_ROOT && page.entries.empty());
}
