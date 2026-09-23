#pragma once

#include <musicrat/config.hpp>
#include <musicrat/protocol/control_events.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Parameters {

using ControlMappingMode = uint8_t;
inline constexpr ControlMappingMode CONTROL_MAP_ABSOLUTE = 0;
inline constexpr ControlMappingMode CONTROL_MAP_RELATIVE = 1;
inline constexpr ControlMappingMode CONTROL_MAP_TOGGLE = 2;
inline constexpr ControlMappingMode CONTROL_MAP_MOMENTARY = 3;
inline constexpr ControlMappingMode CONTROL_MAP_GATE = 4;
inline constexpr ControlMappingMode CONTROL_MAP_TRIGGER = 5;
inline constexpr ControlMappingMode CONTROL_MAP_CHOICE = 6;

using ControlMappingCurve = uint8_t;
inline constexpr ControlMappingCurve CONTROL_CURVE_LINEAR = 0;
inline constexpr ControlMappingCurve CONTROL_CURVE_LOGARITHMIC = 1;
inline constexpr ControlMappingCurve CONTROL_CURVE_EXPONENTIAL = 2;

using ControlPickupMode = uint8_t;
inline constexpr ControlPickupMode CONTROL_PICKUP_IMMEDIATE = 0;
inline constexpr ControlPickupMode CONTROL_PICKUP_MATCH = 1;

struct CompiledControlBinding {
    CommRaT::Messages::ControlBindingId binding_id{
        CommRaT::Messages::INVALID_CONTROL_BINDING_ID};
    CommRaT::Messages::ControlDeviceId source_device_id{
        CommRaT::Messages::INVALID_CONTROL_DEVICE_ID};
    CommRaT::Messages::ControlEndpointId source_endpoint_id{
        CommRaT::Messages::INVALID_CONTROL_ENDPOINT_ID};
    uint32_t target_parameter_id{0};
    CommRaT::Messages::ControlEventKind source_kind{
        CommRaT::Messages::CONTROL_UNIPOLAR};
    ControlMappingMode mode{CONTROL_MAP_ABSOLUTE};
    ControlMappingCurve curve{CONTROL_CURVE_LINEAR};
    ControlPickupMode pickup{CONTROL_PICKUP_IMMEDIATE};
    double source_minimum{0.0};
    double source_maximum{1.0};
    double target_minimum{0.0};
    double target_maximum{1.0};
    double scale{1.0};
    double offset{0.0};
    double dead_zone{0.0};
    double quantization{0.0};
    double hysteresis{0.0};
    double pickup_tolerance{0.01};
    double initial_value{0.0};
    bool invert{false};
};

struct ControlMapper {
    static constexpr std::size_t MAX_BINDINGS =
        musicrat::config::max_parameter_events;

    sertial::fixed_vector<CompiledControlBinding, MAX_BINDINGS> bindings{};
};

} // namespace CommRaT::Parameters