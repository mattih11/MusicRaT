#pragma once

#include <musicrat/backends/media/decode_ahead.hpp>
#include <musicrat/backends/media/wav_reader.hpp>

#include <cstddef>

namespace musicrat::backends::media {

template<std::size_t ChunkCount>
using WavDecodeAhead = DecodeAhead<WavReader, ChunkCount>;

} // namespace musicrat::backends::media