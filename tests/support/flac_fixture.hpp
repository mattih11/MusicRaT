#pragma once

#include <FLAC/stream_encoder.h>

#include <cstdint>
#include <filesystem>
#include <vector>

inline bool write_flac_fixture(
    const std::filesystem::path& path,
    uint32_t sample_rate_hz,
    uint32_t channel_count,
    const std::vector<FLAC__int32>& interleaved_samples,
    uint32_t block_size = 0) {
    if (channel_count == 0
        || interleaved_samples.size() % channel_count != 0) {
        return false;
    }

    FLAC__StreamEncoder* encoder = FLAC__stream_encoder_new();
    if (encoder == nullptr) {
        return false;
    }
    const auto frame_count = static_cast<FLAC__uint64>(
        interleaved_samples.size() / channel_count);
    const bool configured = FLAC__stream_encoder_set_channels(
            encoder, channel_count)
        && (block_size == 0
            || FLAC__stream_encoder_set_streamable_subset(encoder, false))
        && FLAC__stream_encoder_set_bits_per_sample(encoder, 16)
        && FLAC__stream_encoder_set_sample_rate(encoder, sample_rate_hz)
        && FLAC__stream_encoder_set_total_samples_estimate(encoder, frame_count)
        && (block_size == 0
            || FLAC__stream_encoder_set_blocksize(encoder, block_size));
    const bool initialized = configured
        && FLAC__stream_encoder_init_file(
            encoder, path.c_str(), nullptr, nullptr)
            == FLAC__STREAM_ENCODER_INIT_STATUS_OK;
    const bool encoded = initialized
        && FLAC__stream_encoder_process_interleaved(
            encoder,
            interleaved_samples.data(),
            static_cast<uint32_t>(frame_count));
    const bool finished = initialized && FLAC__stream_encoder_finish(encoder);
    FLAC__stream_encoder_delete(encoder);
    return encoded && finished;
}