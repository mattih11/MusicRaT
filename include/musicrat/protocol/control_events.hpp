#pragma once

#include <musicrat/config.hpp>
#include <musicrat/protocol/control_ids.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

using ControlEventKind = uint8_t;

inline constexpr ControlEventKind CONTROL_UNIPOLAR = 0;
inline constexpr ControlEventKind CONTROL_BIPOLAR = 1;
inline constexpr ControlEventKind CONTROL_RELATIVE = 2;
inline constexpr ControlEventKind CONTROL_BOOLEAN = 3;
inline constexpr ControlEventKind CONTROL_CHOICE = 4;
inline constexpr ControlEventKind CONTROL_GATE = 5;
inline constexpr ControlEventKind CONTROL_TRIGGER = 6;

enum ControlEventFlag : uint16_t {
    CONTROL_EVENT_GESTURE_BEGIN = 1U << 0U,
    CONTROL_EVENT_GESTURE_UPDATE = 1U << 1U,
    CONTROL_EVENT_GESTURE_END = 1U << 2U,
    CONTROL_EVENT_GESTURE_CANCEL = 1U << 3U,
};

enum ControlEventBlockFlag : uint16_t {
    CONTROL_EVENT_BLOCK_OVERFLOW = 1U << 0U,
};

struct ControlEvent {
    ControlDeviceId source_device_id{INVALID_CONTROL_DEVICE_ID};
    ControlEndpointId source_endpoint_id{INVALID_CONTROL_ENDPOINT_ID};
    ControlOriginId origin_id{INVALID_CONTROL_ORIGIN_ID};
    uint32_t sample_offset{0};
    ControlEventKind kind{CONTROL_UNIPOLAR};
    double value{0.0};
    uint16_t flags{0};
};

struct ControlEventBlock {
    static constexpr std::size_t MAX_EVENTS = musicrat::config::max_control_events;

    sertial::fixed_vector<ControlEvent, MAX_EVENTS> events{};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
    uint16_t flags{0};
};

} // namespace CommRaT::Messages