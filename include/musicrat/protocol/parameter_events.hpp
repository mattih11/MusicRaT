#pragma once

#include <musicrat/config.hpp>

#include <sertial/containers/fixed_vector.hpp>

#include <cstddef>
#include <cstdint>

namespace CommRaT::Messages {

struct ParameterEvent {
    uint32_t source_endpoint_id{0};
    uint32_t parameter_id{0};
    uint32_t sample_offset{0};
    double value{0.0};
};

struct ParameterEventBlock {
    static constexpr std::size_t MAX_EVENTS = musicrat::config::max_parameter_events;

    sertial::fixed_vector<ParameterEvent, MAX_EVENTS> events{};
    uint64_t timestamp_ns{0};
    uint64_t sequence_number{0};
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