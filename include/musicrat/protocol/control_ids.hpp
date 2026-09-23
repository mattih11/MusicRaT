#pragma once

#include <cstdint>

namespace CommRaT::Messages {

using ControlDeviceId = uint32_t;
using ControlEndpointId = uint32_t;
using ControlOriginId = uint32_t;
using ControlBindingId = uint32_t;

inline constexpr ControlDeviceId INVALID_CONTROL_DEVICE_ID = 0;
inline constexpr ControlEndpointId INVALID_CONTROL_ENDPOINT_ID = 0;
inline constexpr ControlOriginId INVALID_CONTROL_ORIGIN_ID = 0;
inline constexpr ControlBindingId INVALID_CONTROL_BINDING_ID = 0;

} // namespace CommRaT::Messages