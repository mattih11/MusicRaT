#include <musicrat/modules/oscillator.hpp>

#include <commrat/module_main.hpp>

using MusicRaTSineOscillator = CommRaT::Oscillator<CommRaT::Waveforms::Sin>;

COMMRAT_MODULE_MAIN(MusicRaTSineOscillator)