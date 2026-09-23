#pragma once

#include <musicrat/config.hpp>
#include <musicrat/protocol/control_ids.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

enum ParameterStateBlockFlag : uint16_t {
    PARAMETER_STATE_BLOCK_OVERFLOW = 1U << 0U,
    PARAMETER_STATE_BLOCK_SNAPSHOT = 1U << 1U,
};

struct ParameterState {
    uint32_t parameter_id{0};
    ControlEndpointId source_endpoint_id{INVALID_CONTROL_ENDPOINT_ID};
    ControlOriginId origin_id{INVALID_CONTROL_ORIGIN_ID};
    ControlBindingId binding_id{INVALID_CONTROL_BINDING_ID};
    double value{0.0};
};

struct ParameterStateBlock {
    static constexpr std::size_t MAX_STATES = musicrat::config::max_parameter_events;

    sertial::fixed_vector<ParameterState, MAX_STATES> states{};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
    uint16_t flags{0};
};

} // namespace CommRaT::Messages