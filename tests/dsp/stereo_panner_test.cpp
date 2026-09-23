#include <musicrat/dsp/stereo_panner.hpp>

#include <cassert>
#include <cmath>
#include <limits>

namespace {

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 1.0e-6;
}

} // namespace

int main() {
    using namespace CommRaT::Parameters;

    auto gains = musicrat::dsp::stereo_pan_gains(-1.0, PAN_LAW_LINEAR);
    assert(near(gains.left, 1.0));
    assert(near(gains.right, 0.0));

    gains = musicrat::dsp::stereo_pan_gains(0.0, PAN_LAW_LINEAR);
    assert(near(gains.left, 0.5));
    assert(near(gains.right, 0.5));

    gains = musicrat::dsp::stereo_pan_gains(1.0, PAN_LAW_EQUAL_POWER);
    assert(near(gains.left, 0.0));
    assert(near(gains.right, 1.0));

    gains = musicrat::dsp::stereo_pan_gains(0.0, PAN_LAW_EQUAL_POWER);
    assert(near(gains.left, std::sqrt(0.5)));
    assert(near(gains.right, std::sqrt(0.5)));

    gains = musicrat::dsp::stereo_pan_gains(2.0, PAN_LAW_LINEAR);
    assert(near(gains.left, 0.0));
    assert(near(gains.right, 1.0));

    gains = musicrat::dsp::stereo_pan_gains(
        std::numeric_limits<musicrat::config::sample_type>::quiet_NaN(),
        PAN_LAW_EQUAL_POWER);
    assert(near(gains.left, std::sqrt(0.5)));
    assert(near(gains.right, std::sqrt(0.5)));
}