#pragma once

#include <musicrat/backends/media/decoder_dispatcher.hpp>
#include <musicrat/backends/media/wav_reader.hpp>

#if MUSICRAT_HAS_FLAC
#include <musicrat/backends/media/flac_reader.hpp>
#endif
#if MUSICRAT_HAS_MP3
#include <musicrat/backends/media/mp3_reader.hpp>
#endif

namespace musicrat::backends::media {

#if MUSICRAT_HAS_FLAC && MUSICRAT_HAS_MP3
using MediaDecoder = DecoderDispatcher<FlacReader, Mp3Reader, WavReader>;
#elif MUSICRAT_HAS_FLAC
using MediaDecoder = DecoderDispatcher<FlacReader, WavReader>;
#elif MUSICRAT_HAS_MP3
using MediaDecoder = DecoderDispatcher<Mp3Reader, WavReader>;
#else
using MediaDecoder = DecoderDispatcher<WavReader>;
#endif

static_assert(DecoderBackend<MediaDecoder>);

} // namespace musicrat::backends::media