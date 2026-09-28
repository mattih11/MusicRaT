#pragma once

#include <musicrat/protocol/audio_block.hpp>
#include <musicrat/protocol/channel_mapper.hpp>
#include <musicrat/utility/audio_block.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace musicrat::dsp {

enum class ChannelMapperConfigError : uint8_t {
    None,
    InvalidChannelCount,
    InvalidMode,
    InvalidPresetChannels,
    InvalidMatrix,
};

class ChannelMapperProcessor {
public:
    using Sample = CommRaT::Messages::AudioBlock::Sample;
    static constexpr std::size_t MAX_CHANNELS =
        CommRaT::Messages::AudioBlock::MAX_CHANNELS;
    static constexpr std::size_t MAX_COEFFICIENTS =
        MAX_CHANNELS * MAX_CHANNELS;

    [[nodiscard]] ChannelMapperConfigError configure(
        const CommRaT::Parameters::ChannelMapper& parameters) noexcept {
        using namespace CommRaT::Parameters;

        if (parameters.input_channel_count == 0
            || parameters.input_channel_count > MAX_CHANNELS
            || parameters.output_channel_count == 0
            || parameters.output_channel_count > MAX_CHANNELS) {
            configured_ = false;
            return ChannelMapperConfigError::InvalidChannelCount;
        }

        coefficients_.fill(Sample{0});
        input_channel_count_ = parameters.input_channel_count;
        output_channel_count_ = parameters.output_channel_count;

        switch (parameters.mode) {
        case CHANNEL_MAP_IDENTITY:
            if (input_channel_count_ != output_channel_count_) {
                return fail(ChannelMapperConfigError::InvalidPresetChannels);
            }
            for (uint16_t channel = 0; channel < input_channel_count_; ++channel) {
                coefficient(channel, channel) = Sample{1};
            }
            break;
        case CHANNEL_MAP_MONO_TO_STEREO:
            if (input_channel_count_ != 1 || output_channel_count_ != 2) {
                return fail(ChannelMapperConfigError::InvalidPresetChannels);
            }
            coefficient(0, 0) = Sample{1};
            coefficient(1, 0) = Sample{1};
            break;
        case CHANNEL_MAP_STEREO_TO_MONO:
            if (input_channel_count_ != 2 || output_channel_count_ != 1) {
                return fail(ChannelMapperConfigError::InvalidPresetChannels);
            }
            coefficient(0, 0) = Sample{0.5};
            coefficient(0, 1) = Sample{0.5};
            break;
        case CHANNEL_MAP_SWAP_STEREO:
            if (!stereo_to_stereo()) {
                return fail(ChannelMapperConfigError::InvalidPresetChannels);
            }
            coefficient(0, 1) = Sample{1};
            coefficient(1, 0) = Sample{1};
            break;
        case CHANNEL_MAP_COPY_LEFT:
        case CHANNEL_MAP_COPY_RIGHT: {
            if (!stereo_to_stereo()) {
                return fail(ChannelMapperConfigError::InvalidPresetChannels);
            }
            const uint16_t source = parameters.mode == CHANNEL_MAP_COPY_LEFT ? 0 : 1;
            coefficient(0, source) = Sample{1};
            coefficient(1, source) = Sample{1};
            break;
        }
        case CHANNEL_MAP_MATRIX: {
            const std::size_t expected = static_cast<std::size_t>(
                input_channel_count_) * output_channel_count_;
            if (parameters.coefficients.size() != expected) {
                return fail(ChannelMapperConfigError::InvalidMatrix);
            }
            for (std::size_t index = 0; index < expected; ++index) {
                if (!std::isfinite(parameters.coefficients[index])) {
                    return fail(ChannelMapperConfigError::InvalidMatrix);
                }
                coefficients_[index] = parameters.coefficients[index];
            }
            break;
        }
        default:
            return fail(ChannelMapperConfigError::InvalidMode);
        }

        configured_ = true;
        return ChannelMapperConfigError::None;
    }

    void process(
        const CommRaT::Messages::AudioBlock& input,
        CommRaT::Messages::AudioBlock& output) const noexcept {
        clear_audio_channels(output);
        copy_audio_metadata(input, output);

        if (!configured_
            || validate_audio_block(input) != AudioBlockValidationError::None
            || input.channel_count != input_channel_count_) {
            mark_invalid_audio_block(output);
            return;
        }

        output.channel_count = output_channel_count_;
        for (uint16_t output_channel = 0;
             output_channel < output_channel_count_;
             ++output_channel) {
            for (uint32_t frame = 0; frame < input.frame_count; ++frame) {
                Sample mixed{0};
                for (uint16_t input_channel = 0;
                     input_channel < input_channel_count_;
                     ++input_channel) {
                    mixed += input.channels[input_channel][frame]
                        * coefficient(output_channel, input_channel);
                }
                output.channels[output_channel].push_back(mixed);
            }
        }
    }

private:
    [[nodiscard]] ChannelMapperConfigError fail(
        ChannelMapperConfigError error) noexcept {
        configured_ = false;
        return error;
    }

    [[nodiscard]] bool stereo_to_stereo() const noexcept {
        return input_channel_count_ == 2 && output_channel_count_ == 2;
    }

    Sample& coefficient(uint16_t output_channel, uint16_t input_channel) noexcept {
        return coefficients_[static_cast<std::size_t>(output_channel)
            * input_channel_count_ + input_channel];
    }

    [[nodiscard]] Sample coefficient(
        uint16_t output_channel, uint16_t input_channel) const noexcept {
        return coefficients_[static_cast<std::size_t>(output_channel)
            * input_channel_count_ + input_channel];
    }

    std::array<Sample, MAX_COEFFICIENTS> coefficients_{};
    uint16_t input_channel_count_{0};
    uint16_t output_channel_count_{0};
    bool configured_{false};
};

} // namespace musicrat::dsp