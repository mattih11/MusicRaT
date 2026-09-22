#include <musicrat/modules/transport_source.hpp>

#include <rfl/json.hpp>

#include <cassert>
#include <chrono>
#include <cmath>

namespace {

class TestTransportSource : public CommRaT::TransportSource {
public:
    using CommRaT::TransportSource::TransportSource;
    using CommRaT::TransportSource::process;
};

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 1.0e-9;
}

} // namespace

int main() {
    commrat::ModuleConfig config{};
    config.name = "TransportSourceTest";
    config.outputs = commrat::MultiOutputConfig{.addresses = {{
        .system_id = 50,
        .instance_id = 1,
    }}};
    config.period = std::chrono::milliseconds{10};
    config.params = rfl::json::read<rfl::Generic>(
        "{\"tempo_bpm\":120.0,\"initial_beat_position\":4.0,"
        "\"sample_rate_hz\":48000,\"beats_per_bar\":4,"
        "\"beat_unit\":4,\"state\":1}").value();

    TestTransportSource source{config};
    CommRaT::Messages::TransportBlock first{};
    CommRaT::Messages::TransportBlock second{};
    source.process(first);
    source.process(second);

    assert(near(first.beat_position, 4.0));
    assert(near(second.beat_position, 4.02));
    assert(first.transport_frame == 0);
    assert(second.transport_frame == 480);
    assert(first.sequence_number == 0);
    assert(second.sequence_number == 1);
    assert(first.state == CommRaT::Messages::TRANSPORT_PLAYING);
    assert((first.flags & CommRaT::Messages::TRANSPORT_DISCONTINUITY) != 0);
    assert(second.flags == 0);
}