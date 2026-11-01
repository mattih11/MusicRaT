#pragma once

#include <cstdint>

namespace CommRaT::Messages {

using MediaLoadState = uint8_t;
inline constexpr MediaLoadState MEDIA_LOAD_IDLE = 0;
inline constexpr MediaLoadState MEDIA_LOAD_PREPARING = 1;
inline constexpr MediaLoadState MEDIA_LOAD_PREPARED = 2;
inline constexpr MediaLoadState MEDIA_LOAD_COMMITTED = 3;
inline constexpr MediaLoadState MEDIA_LOAD_FAILED = 4;
inline constexpr MediaLoadState MEDIA_LOAD_PROTECTED = 5;
inline constexpr MediaLoadState MEDIA_LOAD_CANCELLED = 6;

using MediaLoadRejection = uint8_t;
inline constexpr MediaLoadRejection MEDIA_LOAD_REJECTION_NONE = 0;
inline constexpr MediaLoadRejection MEDIA_LOAD_REJECTION_INVALID_PATH = 1;
inline constexpr MediaLoadRejection MEDIA_LOAD_REJECTION_BUSY = 2;
inline constexpr MediaLoadRejection MEDIA_LOAD_REJECTION_ID_EXHAUSTED = 3;

} // namespace CommRaT::Messages
