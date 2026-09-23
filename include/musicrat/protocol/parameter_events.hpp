#pragma once

#include <musicrat/config.hpp>
#include <musicrat/protocol/control_ids.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

enum ParameterEventBlockFlag : uint16_t {
    PARAMETER_EVENT_BLOCK_OVERFLOW = 1U << 0U,
    PARAMETER_EVENT_BLOCK_SOURCE_OVERFLOW = 1U << 1U,
    PARAMETER_EVENT_BLOCK_INVALID_CONFIG = 1U << 2U,
};

struct ParameterEvent {
    ControlEndpointId source_endpoint_id{INVALID_CONTROL_ENDPOINT_ID};
    uint32_t parameter_id{0};
    uint32_t sample_offset{0};
    double value{0.0};
    ControlOriginId origin_id{INVALID_CONTROL_ORIGIN_ID};
    ControlBindingId binding_id{INVALID_CONTROL_BINDING_ID};
};

struct ParameterEventBlock {
    static constexpr std::size_t MAX_EVENTS = musicrat::config::max_parameter_events;

    sertial::fixed_vector<ParameterEvent, MAX_EVENTS> events{};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
    uint16_t flags{0};
};

} // namespace CommRaT::Messages

namespace CommRaT::Parameters {

inline constexpr uint32_t PARAMETER_SOURCE_ENDPOINT_ID_PARAMETER_ID = 1;
inline constexpr uint32_t PARAMETER_SOURCE_PARAMETER_ID_PARAMETER_ID = 2;
inline constexpr uint32_t PARAMETER_SOURCE_VALUE_PARAMETER_ID = 3;

struct ParameterSource {
    uint32_t source_endpoint_id{1};
    uint32_t parameter_id{1};
    double value{1.0};
};

} // namespace CommRaT::Parameters