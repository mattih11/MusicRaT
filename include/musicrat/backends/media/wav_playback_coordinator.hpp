#pragma once

#include <musicrat/backends/media/playback_coordinator.hpp>
#include <musicrat/backends/media/wav_reader.hpp>

#include <cstddef>

namespace musicrat::backends::media {

template<std::size_t ChunkCount>
using WavPlaybackCoordinator = PlaybackCoordinator<WavReader, ChunkCount>;

} // namespace musicrat::backends::media