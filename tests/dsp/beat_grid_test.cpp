#include <musicrat/dsp/beat_grid.hpp>

#include <cassert>
#include <cmath>

namespace {

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 1.0e-6;
}

} // namespace

int main() {
    CommRaT::Messages::BeatGrid grid{};
    grid.sample_rate_hz = 48000.0;
    grid.beats_per_bar = 4;
    grid.beat_unit = 4;
    grid.segments.push_back({
        .start_frame = 0,
        .start_beat = 0.0,
        .tempo_bpm = 120.0,
    });
    grid.segments.push_back({
        .start_frame = 96000,
        .start_beat = 4.0,
        .tempo_bpm = 60.0,
    });

    const musicrat::dsp::BeatGridMapper mapper{grid};
    assert(mapper.valid());
    auto position = mapper.position_at_frame(72000.0);
    assert(position.valid);
    assert(near(position.beat_position, 3.0));
    assert(position.bar == 0);
    assert(position.beat_in_bar == 3);
    assert(near(position.beat_fraction, 0.0));

    position = mapper.position_at_frame(120000.0);
    assert(position.valid);
    assert(near(position.beat_position, 4.5));
    assert(position.bar == 1);
    assert(position.beat_in_bar == 0);
    assert(near(position.beat_fraction, 0.5));
    assert(near(mapper.frame_at_beat(4.5), 120000.0));
    assert(near(mapper.tempo_at_frame(95999.0), 120.0));
    assert(near(mapper.tempo_at_frame(96000.0), 60.0));

    grid.segments[1].start_beat = 5.0;
    assert(!musicrat::dsp::BeatGridMapper{grid}.valid());
}