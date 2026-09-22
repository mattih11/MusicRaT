#include <musicrat/dsp/transport_follower.hpp>

#include <cassert>
#include <cmath>

namespace {

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 1.0e-6;
}

CommRaT::Messages::BeatGrid make_grid() {
    CommRaT::Messages::BeatGrid grid{};
    grid.sample_rate_hz = 48000.0;
    grid.beats_per_bar = 4;
    grid.beat_unit = 4;
    grid.segments.push_back({
        .start_frame = 0,
        .start_beat = 0.0,
        .tempo_bpm = 120.0,
    });
    return grid;
}

} // namespace

int main() {
    const musicrat::dsp::TransportFollower follower{make_grid()};
    assert(follower.valid());

    CommRaT::Messages::TransportBlock transport{};
    transport.state = CommRaT::Messages::TRANSPORT_PLAYING;
    transport.tempo_bpm = 126.0;
    transport.beat_position = 4.0;

    auto decision = follower.update(
        transport, CommRaT::Messages::TRANSPORT_SYNC_OFF, 96000.0);
    assert(decision.valid);
    assert(decision.playing);
    assert(decision.action == musicrat::dsp::TransportSyncAction::None);

    decision = follower.update(
        transport, CommRaT::Messages::TRANSPORT_SYNC_TEMPO, 96000.0);
    assert(decision.valid);
    assert(decision.action == musicrat::dsp::TransportSyncAction::SetRate);
    assert(near(decision.target_rate, 1.05));

    transport.tempo_bpm = 120.0;
    transport.beat_position = 4.25;
    decision = follower.update(
        transport, CommRaT::Messages::TRANSPORT_SYNC_BEAT, 96000.0);
    assert(decision.valid);
    assert(decision.action == musicrat::dsp::TransportSyncAction::SetRate);
    assert(near(decision.phase_error_beats, 0.25));
    assert(near(decision.target_rate, 1.025));

    transport.beat_position = 5.0;
    decision = follower.update(
        transport, CommRaT::Messages::TRANSPORT_SYNC_BEAT, 96000.0);
    assert(decision.valid);
    assert(decision.action == musicrat::dsp::TransportSyncAction::Seek);
    assert(decision.seek_frame == 120000);

    transport.beat_position = 4.1;
    transport.flags = CommRaT::Messages::TRANSPORT_DISCONTINUITY;
    decision = follower.update(
        transport, CommRaT::Messages::TRANSPORT_SYNC_BEAT, 96000.0);
    assert(decision.action == musicrat::dsp::TransportSyncAction::Seek);
    assert(decision.seek_frame == 98400);

    transport.state = CommRaT::Messages::TRANSPORT_PAUSED;
    transport.flags = 0;
    decision = follower.update(
        transport, CommRaT::Messages::TRANSPORT_SYNC_TEMPO, 96000.0);
    assert(decision.valid);
    assert(!decision.playing);
}