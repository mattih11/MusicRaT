#pragma once

#include <musicrat/config.hpp>
#include <musicrat/protocol/control_events.hpp>

#include <sertial/containers/fixed_string.hpp>
#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

using WidgetProperty = uint8_t;
inline constexpr WidgetProperty WIDGET_PROPERTY_VALUE = 0;
inline constexpr WidgetProperty WIDGET_PROPERTY_ACTIVE = 1;
inline constexpr WidgetProperty WIDGET_PROPERTY_TEXT = 2;

using WidgetValueKind = uint8_t;
inline constexpr WidgetValueKind WIDGET_VALUE_CONTINUOUS = 0;
inline constexpr WidgetValueKind WIDGET_VALUE_BOOLEAN = 1;
inline constexpr WidgetValueKind WIDGET_VALUE_CHOICE = 2;
inline constexpr WidgetValueKind WIDGET_VALUE_TEXT = 3;

struct WidgetUpdate {
    ControlBindingId binding_id{INVALID_CONTROL_BINDING_ID};
    sertial::fixed_string<64> surface_id{};
    sertial::fixed_string<64> widget_id{};
    WidgetProperty property{WIDGET_PROPERTY_VALUE};
    WidgetValueKind kind{WIDGET_VALUE_CONTINUOUS};
    double value{0.0};
    bool active{false};
    sertial::fixed_string<128> text{};
};

enum WidgetUpdateBlockFlag : uint16_t {
    WIDGET_UPDATE_BLOCK_SOURCE_INVALID = 1U << 0U,
    WIDGET_UPDATE_BLOCK_OVERFLOW = 1U << 1U,
};

struct WidgetUpdateBlock {
    static constexpr std::size_t MAX_EVENTS = musicrat::config::max_parameter_events;

    sertial::fixed_vector<WidgetUpdate, MAX_EVENTS> events{};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
    uint16_t flags{0};
};

} // namespace CommRaT::Messages

namespace CommRaT::Parameters {

using LevelMeterObservationSelector = uint8_t;
inline constexpr LevelMeterObservationSelector LEVEL_METER_OBSERVE_PEAK = 0;
inline constexpr LevelMeterObservationSelector LEVEL_METER_OBSERVE_RMS = 1;
inline constexpr LevelMeterObservationSelector LEVEL_METER_OBSERVE_CLIPPED = 2;

struct CompiledLevelMeterObservationBinding {
    CommRaT::Messages::ControlBindingId binding_id{
        CommRaT::Messages::INVALID_CONTROL_BINDING_ID};
    sertial::fixed_string<64> surface_id{};
    sertial::fixed_string<64> widget_id{};
    CommRaT::Messages::WidgetProperty property{
        CommRaT::Messages::WIDGET_PROPERTY_VALUE};
    LevelMeterObservationSelector selector{LEVEL_METER_OBSERVE_PEAK};
    uint16_t channel{0};
};

struct LevelMeterUiAdapter {
    static constexpr std::size_t MAX_BINDINGS = musicrat::config::max_parameter_events;

    sertial::fixed_vector<CompiledLevelMeterObservationBinding, MAX_BINDINGS> bindings{};
};

} // namespace CommRaT::Parameters
