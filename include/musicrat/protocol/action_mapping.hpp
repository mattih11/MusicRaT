#pragma once

#include <musicrat/config.hpp>
#include <musicrat/protocol/control_events.hpp>
#include <musicrat/protocol/deck_control.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Parameters {

struct CompiledActionBinding {
    CommRaT::Messages::ControlBindingId binding_id{
        CommRaT::Messages::INVALID_CONTROL_BINDING_ID};
    CommRaT::Messages::ControlDeviceId source_device_id{
        CommRaT::Messages::INVALID_CONTROL_DEVICE_ID};
    CommRaT::Messages::ControlEndpointId source_endpoint_id{
        CommRaT::Messages::INVALID_CONTROL_ENDPOINT_ID};
    CommRaT::Messages::ControlEventKind source_kind{
        CommRaT::Messages::CONTROL_TRIGGER};
    CommRaT::Messages::DeckControlType action_type{
        CommRaT::Messages::DECK_CONTROL_PLAY};
    CommRaT::Messages::DeckControlQuantization quantization{
        CommRaT::Messages::DECK_QUANTIZE_IMMEDIATE};
    double source_minimum{0.0};
    double source_maximum{1.0};
    double target_minimum{0.0};
    double target_maximum{1.0};
    double default_value{1.0};
    uint32_t ramp_frames{0};
};

struct ActionMapper {
    static constexpr std::size_t MAX_BINDINGS = musicrat::config::max_parameter_events;

    sertial::fixed_vector<CompiledActionBinding, MAX_BINDINGS> bindings{};
};

} // namespace CommRaT::Parameters
