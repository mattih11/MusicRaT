#include "playback_preroll.hpp"

#include <cassert>
#include <cstdint>

using musicrat::backends::audio::detail::PlaybackPreroll;

int main() {
    PlaybackPreroll preroll;
    preroll.reset(480, 12288);
    assert(!preroll.started());
    assert(!preroll.ready(480, 1024));
    assert(!preroll.ready(1920, 1024));
    assert(preroll.ready(2400, 1024));
    assert(preroll.started());
    assert(preroll.ready(1376, 1024));
    assert(preroll.ready(0, 1024));
    preroll.rebuffer();
    assert(!preroll.ready(480, 1024));
    assert(preroll.started());
    assert(preroll.ready(2400, 1024));

    preroll.reset(480, 12288);
    assert(!preroll.ready(480, 240));
    assert(preroll.ready(960, 240));

    preroll.reset(480, 12288, 960);
    assert(!preroll.ready(960, 240));
    assert(!preroll.ready(1440, 240));
    assert(preroll.ready(1920, 240));

    preroll.reset(4096, 12288);
    assert(!preroll.ready(4096, 1024));
    assert(preroll.ready(8192, 1024));
    preroll.reset(1, 3);
    assert(!preroll.ready(1, 1));
    assert(preroll.ready(2, 1));
    preroll.reset(4096, 12288);
    assert(preroll.ready(12288, 8192));

    // A 480-frame source and a 1024-frame consumer have independent phases.
    for (uint64_t phase = 0; phase < 480; phase += 17) {
        preroll.reset(480, 12288);
        uint64_t queued = 0;
        uint64_t consumed = 0;
        uint64_t produced = 0;
        for (uint64_t time = 0; time < 480000; ++time) {
            // Alternate early and late arrivals without changing the source rate.
            const uint64_t arrival = time % 960;
            if (arrival == 0 || arrival == 480 + 200) {
                queued += 480;
                produced += 480;
            }
            if (time >= phase && (time - phase) % 1024 == 0
                && preroll.ready(queued, 1024)) {
                assert(queued >= 1024);
                queued -= 1024;
                consumed += 1024;
            }
            assert(queued <= 12288);
            assert(produced == consumed + queued);
        }
        assert(consumed > 470000);
    }
}
