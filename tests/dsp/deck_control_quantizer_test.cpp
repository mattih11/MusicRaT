#include <musicrat/dsp/deck_control_quantizer.hpp>

#include <cassert>

int main() {
    musicrat::dsp::DeckControlQuantizer quantizer{};
    CommRaT::Messages::TransportBlock transport{};
    transport.beat_position = 0.99;
    transport.tempo_bpm = 120.0;
    transport.sample_rate_hz = 48000;
    transport.beats_per_bar = 4;
    transport.state = CommRaT::Messages::TRANSPORT_PLAYING;

    CommRaT::Messages::DeckControlEventBlock input{};
    input.events.push_back({
        .type = CommRaT::Messages::DECK_CONTROL_PLAY,
        .quantization = CommRaT::Messages::DECK_QUANTIZE_BEAT,
    });
    CommRaT::Messages::DeckControlEventBlock output{};
    auto result = quantizer.process(&input, &transport, 480, output);
    assert(result.pending_count == 0);
    assert(result.dropped_count == 0);
    assert(output.events.size() == 1);
    assert(output.events[0].sample_offset == 240);

    quantizer.reset();
    transport.beat_position = 3.5;
    input.events[0].type = CommRaT::Messages::DECK_CONTROL_RETURN_TO_CUE;
    input.events[0].quantization = CommRaT::Messages::DECK_QUANTIZE_BAR;
    result = quantizer.process(&input, &transport, 480, output);
    assert(result.pending_count == 1);
    assert(output.events.empty());

    transport.beat_position = 3.99;
    result = quantizer.process(nullptr, &transport, 480, output);
    assert(result.pending_count == 0);
    assert(output.events.size() == 1);
    assert(output.events[0].sample_offset == 240);
    assert(output.events[0].type
        == CommRaT::Messages::DECK_CONTROL_RETURN_TO_CUE);

    quantizer.reset();
    result = quantizer.process(&input, nullptr, 480, output);
    assert(result.pending_count == 1);
    assert(result.dropped_count == 0);
    assert(output.events.empty());
    transport.beat_position = 3.99;
    result = quantizer.process(nullptr, &transport, 480, output);
    assert(result.pending_count == 0);
    assert(output.events.size() == 1);
    assert(output.events[0].sample_offset == 240);

    transport.state = CommRaT::Messages::TRANSPORT_PAUSED;
    transport.beat_position = 7.99;
    input.events[0].type = CommRaT::Messages::DECK_CONTROL_ENABLE_LOOP;
    result = quantizer.process(&input, &transport, 480, output);
    assert(result.pending_count == 1);
    assert(output.events.empty());
    transport.state = CommRaT::Messages::TRANSPORT_PLAYING;
    result = quantizer.process(nullptr, &transport, 480, output);
    assert(result.pending_count == 0);
    assert(output.events.size() == 1);
    assert(output.events[0].sample_offset == 240);
}