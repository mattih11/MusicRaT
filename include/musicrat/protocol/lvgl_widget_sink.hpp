#pragma once

#include <musicrat/config.hpp>
#include <musicrat/protocol/control_events.hpp>

#include <sertial/containers/fixed_string.hpp>
#include <sertial/containers/fixed_vector.hpp>

#include <cstdint>

namespace CommRaT::Parameters {

inline constexpr uint32_t LVGL_WIDGET_SINK_MANIFEST_PATH_PARAMETER_ID = 1;
inline constexpr uint32_t LVGL_WIDGET_SINK_DISPLAY_INSTANCE_PARAMETER_ID = 2;
inline constexpr uint32_t LVGL_WIDGET_SINK_WIDTH_PARAMETER_ID = 3;
inline constexpr uint32_t LVGL_WIDGET_SINK_HEIGHT_PARAMETER_ID = 4;
inline constexpr uint32_t LVGL_WIDGET_SINK_BACKEND_PARAMETER_ID = 5;
inline constexpr uint32_t LVGL_WIDGET_SINK_DEVICE_PATH_PARAMETER_ID = 6;
inline constexpr uint32_t LVGL_WIDGET_SINK_CONNECTOR_ID_PARAMETER_ID = 7;
inline constexpr uint32_t LVGL_WIDGET_SINK_WINDOW_TITLE_PARAMETER_ID = 8;
inline constexpr uint32_t LVGL_WIDGET_SINK_DEVICE_ID_PARAMETER_ID = 9;

inline constexpr uint8_t LVGL_DISPLAY_BACKEND_HEADLESS = 0;
inline constexpr uint8_t LVGL_DISPLAY_BACKEND_DRM = 1;
inline constexpr uint8_t LVGL_DISPLAY_BACKEND_FRAMEBUFFER = 2;
inline constexpr uint8_t LVGL_DISPLAY_BACKEND_SDL = 3;

struct LvglWidgetControlBinding {
    sertial::fixed_string<64> widget_id{};
    CommRaT::Messages::ControlEndpointId endpoint_id{
        CommRaT::Messages::INVALID_CONTROL_ENDPOINT_ID};
    CommRaT::Messages::ControlOriginId origin_id{
        CommRaT::Messages::INVALID_CONTROL_ORIGIN_ID};
    CommRaT::Messages::ControlEventKind kind{
        CommRaT::Messages::CONTROL_UNIPOLAR};
};

struct LvglWidgetSink {
    sertial::fixed_string<512> manifest_path{};
    sertial::fixed_string<128> display_instance_id{};
    uint32_t width{800};
    uint32_t height{480};
    uint8_t backend{LVGL_DISPLAY_BACKEND_HEADLESS};
    sertial::fixed_string<256> device_path{};
    int64_t connector_id{-1};
    sertial::fixed_string<128> window_title{"MusicRaT"};
    CommRaT::Messages::ControlDeviceId device_id{
        CommRaT::Messages::INVALID_CONTROL_DEVICE_ID};
    sertial::fixed_vector<
        LvglWidgetControlBinding,
        musicrat::config::max_control_events> control_bindings{};
};

} // namespace CommRaT::Parameters