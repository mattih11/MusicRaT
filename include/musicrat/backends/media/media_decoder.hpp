#pragma once

#include <musicrat/backends/media/decoder_dispatcher.hpp>
#include <musicrat/backends/media/wav_reader.hpp>

#if MUSICRAT_HAS_FLAC
#include <musicrat/backends/media/flac_reader.hpp>
#endif
#if MUSICRAT_HAS_MP3
#include <musicrat/backends/media/mp3_reader.hpp>
#endif
#if MUSICRAT_HAS_AAC
#include <musicrat/backends/media/aac_reader.hpp>
#endif
#if MUSICRAT_HAS_OPUS
#include <musicrat/backends/media/opus_reader.hpp>
#endif

namespace musicrat::backends::media {

namespace detail {

template<typename... Backends>
struct DecoderList {};

template<typename Backend, typename List>
struct PrependDecoder;

template<typename Backend, typename... Backends>
struct PrependDecoder<Backend, DecoderList<Backends...>> {
    using type = DecoderList<Backend, Backends...>;
};

template<typename List>
struct MakeDecoderDispatcher;

template<typename... Backends>
struct MakeDecoderDispatcher<DecoderList<Backends...>> {
    using type = DecoderDispatcher<Backends...>;
};

using WavDecoderList = DecoderList<WavReader>;

#if MUSICRAT_HAS_MP3
using Mp3DecoderList = typename PrependDecoder<Mp3Reader, WavDecoderList>::type;
#else
using Mp3DecoderList = WavDecoderList;
#endif

#if MUSICRAT_HAS_OPUS
using OpusDecoderList = typename PrependDecoder<OpusReader, Mp3DecoderList>::type;
#else
using OpusDecoderList = Mp3DecoderList;
#endif

#if MUSICRAT_HAS_AAC
using AacDecoderList = typename PrependDecoder<AacReader, OpusDecoderList>::type;
#else
using AacDecoderList = OpusDecoderList;
#endif

#if MUSICRAT_HAS_FLAC
using MediaDecoderList = typename PrependDecoder<FlacReader, AacDecoderList>::type;
#else
using MediaDecoderList = AacDecoderList;
#endif

} // namespace detail

using MediaDecoder = typename detail::MakeDecoderDispatcher<
    detail::MediaDecoderList>::type;

static_assert(DecoderBackend<MediaDecoder>);

} // namespace musicrat::backends::media