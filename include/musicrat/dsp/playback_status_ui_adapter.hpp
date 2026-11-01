#pragma once

#include <musicrat/protocol/playback_status.hpp>
#include <musicrat/protocol/playback_ui.hpp>

#include <sertial/containers/rt_format.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace musicrat::dsp {

enum class PlaybackStatusUiAdapterConfigError : uint8_t {
    None,
    TooManyBindings,
    InvalidBinding,
    DuplicateBindingId,
};

struct PlaybackStatusUiAdapterResult {
    uint32_t mapped_count{0};
    uint32_t unavailable_count{0};
    uint32_t invalid_count{0};
};

class PlaybackStatusUiAdapter {
public:
    static constexpr std::size_t MAX_BINDINGS =
        CommRaT::Parameters::PlaybackStatusUiAdapter::MAX_BINDINGS;
    static_assert(MAX_BINDINGS <= CommRaT::Messages::WidgetUpdateBlock::MAX_EVENTS);

    template<std::size_t Size>
    [[nodiscard]] PlaybackStatusUiAdapterConfigError configure(
        const std::array<CommRaT::Parameters::CompiledPlaybackObservationBinding, Size>& bindings) noexcept {
        return configure(bindings.data(), bindings.size());
    }

    [[nodiscard]] PlaybackStatusUiAdapterConfigError configure(
        const CommRaT::Parameters::CompiledPlaybackObservationBinding* bindings,
        std::size_t count) noexcept {
        if (count > MAX_BINDINGS) {
            return PlaybackStatusUiAdapterConfigError::TooManyBindings;
        }
        if (bindings == nullptr && count != 0) {
            return PlaybackStatusUiAdapterConfigError::InvalidBinding;
        }
        for (std::size_t index = 0; index < count; ++index) {
            if (!valid_binding(bindings[index])) {
                return PlaybackStatusUiAdapterConfigError::InvalidBinding;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (bindings[previous].binding_id == bindings[index].binding_id) {
                    return PlaybackStatusUiAdapterConfigError::DuplicateBindingId;
                }
            }
        }
        for (std::size_t index = 0; index < count; ++index) {
            bindings_[index] = bindings[index];
        }
        count_ = count;
        return PlaybackStatusUiAdapterConfigError::None;
    }

    [[nodiscard]] PlaybackStatusUiAdapterResult process(
        const CommRaT::Messages::PlaybackStatusBlock& input,
        CommRaT::Messages::WidgetUpdateBlock& output) const noexcept {
        using namespace CommRaT::Messages;
        using namespace CommRaT::Parameters;
        output.events.clear();
        output.timestamp_ns = input.timestamp_ns;
        output.sequence_number = input.sequence_number;
        output.flags = 0;
        PlaybackStatusUiAdapterResult result{};
        for (std::size_t index = 0; index < count_; ++index) {
            const auto& binding = bindings_[index];
            WidgetUpdate update{
                .binding_id = binding.binding_id,
                .surface_id = binding.surface_id,
                .widget_id = binding.widget_id,
                .property = binding.property,
            };
            const bool source_valid = (input.flags & PLAYBACK_STATUS_INVALID) == 0
                || binding.selector == PLAYBACK_OBSERVE_LOAD_STATE
                || binding.selector == PLAYBACK_OBSERVE_LOAD_DECODE_ERROR;
            bool available = source_valid;
            bool valid = source_valid;
            if (is_time(binding.selector)) {
                update.kind = WIDGET_VALUE_TEXT;
                update.text = "--:--.---";
                valid = valid && std::isfinite(input.media_position_frames)
                    && input.media_position_frames >= 0.0;
                available = valid && input.source_sample_rate_hz != 0;
                if (available) {
                    const long double position = input.media_position_frames;
                    if (binding.selector == PLAYBACK_OBSERVE_ELAPSED_TIME) {
                        valid = format_time(position, input.source_sample_rate_hz, update);
                    } else if (input.duration_frames != 0) {
                        valid = format_time(
                            std::max(0.0L, static_cast<long double>(input.duration_frames) - position),
                            input.source_sample_rate_hz, update);
                    } else {
                        available = false;
                    }
                }
            } else {
                double value = 0.0;
                if (binding.selector == PLAYBACK_OBSERVE_POSITION && input.duration_frames == 0) {
                    available = false;
                } else {
                    valid = valid && read_value(input, binding.selector, value);
                }
                update.kind = is_boolean(binding.selector) ? WIDGET_VALUE_BOOLEAN
                    : is_choice(binding.selector) ? WIDGET_VALUE_CHOICE
                    : WIDGET_VALUE_CONTINUOUS;
                update.value = update.kind == WIDGET_VALUE_CONTINUOUS
                    ? std::clamp((value - binding.minimum) / (binding.maximum - binding.minimum), 0.0, 1.0)
                    : value;
                update.active = value != 0.0;
            }
            if (!valid) {
                ++result.invalid_count;
            }
            if (!valid || !available) {
                update.kind = WIDGET_VALUE_UNAVAILABLE;
                ++result.unavailable_count;
            }
            output.events.push_back(update);
            ++result.mapped_count;
        }
        return result;
    }

private:
    static bool is_boolean(CommRaT::Parameters::PlaybackObservationSelector selector) noexcept {
        using namespace CommRaT::Parameters;
        return selector >= PLAYBACK_OBSERVE_READY && selector <= PLAYBACK_OBSERVE_LOOPING;
    }

    static bool is_choice(CommRaT::Parameters::PlaybackObservationSelector selector) noexcept {
        using namespace CommRaT::Parameters;
        return selector == PLAYBACK_OBSERVE_WORKER_STATE || selector == PLAYBACK_OBSERVE_DECODE_ERROR
            || selector == PLAYBACK_OBSERVE_LOAD_STATE || selector == PLAYBACK_OBSERVE_LOAD_DECODE_ERROR;
    }

    static bool is_time(CommRaT::Parameters::PlaybackObservationSelector selector) noexcept {
        using namespace CommRaT::Parameters;
        return selector == PLAYBACK_OBSERVE_ELAPSED_TIME || selector == PLAYBACK_OBSERVE_REMAINING_TIME;
    }

    static bool valid_binding(const CommRaT::Parameters::CompiledPlaybackObservationBinding& binding) noexcept {
        using namespace CommRaT::Messages;
        using namespace CommRaT::Parameters;
        if (binding.binding_id == INVALID_CONTROL_BINDING_ID || binding.surface_id.empty()
            || binding.widget_id.empty() || binding.selector > PLAYBACK_OBSERVE_LOAD_DECODE_ERROR) {
            return false;
        }
        if (is_time(binding.selector)) return binding.property == WIDGET_PROPERTY_TEXT;
        if (is_boolean(binding.selector)) return binding.property == WIDGET_PROPERTY_ACTIVE;
        if (binding.property != WIDGET_PROPERTY_VALUE) return false;
        return is_choice(binding.selector)
            || (std::isfinite(binding.minimum) && std::isfinite(binding.maximum)
                && binding.minimum < binding.maximum
                && std::isfinite(binding.maximum - binding.minimum));
    }

    static bool read_value(
        const CommRaT::Messages::PlaybackStatusBlock& input,
        CommRaT::Parameters::PlaybackObservationSelector selector,
        double& value) noexcept {
        using namespace CommRaT::Messages;
        using namespace CommRaT::Parameters;
        switch (selector) {
        case PLAYBACK_OBSERVE_RATE: value = input.playback_rate; break;
        case PLAYBACK_OBSERVE_MEDIA_POSITION: value = input.media_position_frames; break;
        case PLAYBACK_OBSERVE_DURATION: value = static_cast<double>(input.duration_frames); break;
        case PLAYBACK_OBSERVE_BEAT_POSITION: value = input.beat_position; break;
        case PLAYBACK_OBSERVE_READY: value = (input.flags & PLAYBACK_STATUS_READY) != 0; break;
        case PLAYBACK_OBSERVE_PLAYING: value = (input.flags & PLAYBACK_STATUS_PLAYING) != 0; break;
        case PLAYBACK_OBSERVE_LOOPING: value = (input.flags & PLAYBACK_STATUS_LOOPING) != 0; break;
        case PLAYBACK_OBSERVE_WORKER_STATE:
            if (input.worker_state > PLAYBACK_WORKER_ERROR) return false;
            value = input.worker_state;
            break;
        case PLAYBACK_OBSERVE_DECODE_ERROR:
            if (input.decode_error > PLAYBACK_DECODE_ERROR_POOL_FAILURE) return false;
            value = input.decode_error;
            break;
        case PLAYBACK_OBSERVE_LOAD_STATE:
            if (input.load_state > MEDIA_LOAD_CANCELLED) return false;
            value = input.load_state;
            break;
        case PLAYBACK_OBSERVE_LOAD_DECODE_ERROR:
            if (input.load_decode_error > PLAYBACK_DECODE_ERROR_POOL_FAILURE) return false;
            value = input.load_decode_error;
            break;
        case PLAYBACK_OBSERVE_POSITION:
            if (input.duration_frames == 0 || input.media_position_frames < 0.0) return false;
            value = input.media_position_frames / static_cast<double>(input.duration_frames);
            break;
        default: return false;
        }
        return std::isfinite(value);
    }

    static bool format_time(
        long double frames, uint32_t sample_rate,
        CommRaT::Messages::WidgetUpdate& update) noexcept {
        const long double milliseconds = frames * 1000.0L / sample_rate;
        if (milliseconds >= static_cast<long double>(std::numeric_limits<uint64_t>::max())) return false;
        const auto total = static_cast<uint64_t>(milliseconds);
        const auto minutes = total / 60000;
        std::array<char, 8> text{};
        std::size_t size = 0;
        text[size++] = ':';
        text[size++] = static_cast<char>('0' + (total / 1000 % 60) / 10);
        text[size++] = static_cast<char>('0' + (total / 1000 % 60) % 10);
        text[size++] = '.';
        text[size++] = static_cast<char>('0' + total % 1000 / 100);
        text[size++] = static_cast<char>('0' + total % 100 / 10);
        text[size++] = static_cast<char>('0' + total % 10);
        update.text.clear();
        if (minutes < 10) sertial::rt::append(update.text, "0", 1);
        sertial::rt::append(update.text, minutes);
        sertial::rt::append(update.text, text.data(), size);
        return true;
    }

    std::array<CommRaT::Parameters::CompiledPlaybackObservationBinding, MAX_BINDINGS> bindings_{};
    std::size_t count_{0};
};

} // namespace musicrat::dsp
