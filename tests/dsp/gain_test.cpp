#include <musicrat/dsp/gain.hpp>

#include <cassert>
#include <cmath>

int main() {
    using Gain = musicrat::dsp::Gain;
    using Sample = Gain::Sample;

    Gain gain{Sample{0}};
    gain.ramp_to(Sample{1}, 4);

    assert(std::abs(gain.process(Sample{1}) - Sample{0.25}) < Sample{1.0e-6});
    assert(std::abs(gain.process(Sample{1}) - Sample{0.5}) < Sample{1.0e-6});
    assert(std::abs(gain.process(Sample{1}) - Sample{0.75}) < Sample{1.0e-6});
    assert(std::abs(gain.process(Sample{1}) - Sample{1}) < Sample{1.0e-6});
    assert(gain.current_gain() == Sample{1});

    gain.set_immediate(Sample{-1});
    assert(gain.process(Sample{0.5}) == Sample{-0.5});
}