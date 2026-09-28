#pragma once

#include <musicrat/config.hpp>
#include <musicrat/protocol/control_ids.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

enum ControlFeedbackBlockFlag : uint16_t {
    CONTROL_FEEDBACK_BLOCK_OVERFLOW = 1U << 0U,
    CONTROL_FEEDBACK_BLOCK_SOURCE_OVERFLOW = 1U << 1U,
    CONTROL_FEEDBACK_BLOCK_INVALID_CONFIG = 1U << 2U,
};

struct ControlFeedback {
    ControlDeviceId destination_device_id{INVALID_CONTROL_DEVICE_ID};
    ControlEndpointId destination_endpoint_id{INVALID_CONTROL_ENDPOINT_ID};
    ControlOriginId origin_id{INVALID_CONTROL_ORIGIN_ID};
    ControlBindingId binding_id{INVALID_CONTROL_BINDING_ID};
    double value{0.0};
};

struct ControlFeedbackBlock {
    static constexpr std::size_t MAX_EVENTS = musicrat::config::max_control_events;

    sertial::fixed_vector<ControlFeedback, MAX_EVENTS> events{};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
    uint16_t flags{0};
};

} // namespace CommRaT::Messages

namespace CommRaT::Parameters {

struct CompiledControlFeedbackBinding {
    CommRaT::Messages::ControlBindingId binding_id{
        CommRaT::Messages::INVALID_CONTROL_BINDING_ID};
    uint32_t target_parameter_id{0};
    CommRaT::Messages::ControlDeviceId destination_device_id{
        CommRaT::Messages::INVALID_CONTROL_DEVICE_ID};
    CommRaT::Messages::ControlEndpointId destination_endpoint_id{
        CommRaT::Messages::INVALID_CONTROL_ENDPOINT_ID};
    CommRaT::Messages::ControlOriginId suppress_origin_id{
        CommRaT::Messages::INVALID_CONTROL_ORIGIN_ID};
};

struct ControlFeedbackRouter {
    static constexpr std::size_t MAX_BINDINGS =
        musicrat::config::max_parameter_events;

    sertial::fixed_vector<CompiledControlFeedbackBinding, MAX_BINDINGS> bindings{};
};

} // namespace CommRaT::Parameters
