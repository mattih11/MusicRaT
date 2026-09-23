#pragma once

#include <musicrat/backends/media/aac_reader.hpp>

namespace musicrat::backends::media {

using OpusReader = FfmpegAudioReader<AV_CODEC_ID_OPUS, MediaCodec::Opus>;

static_assert(DecoderBackend<OpusReader>);

} // namespace musicrat::backends::media