if(MODE STREQUAL "invalid")
    foreach(arguments IN ITEMS
        "--blocks;invalid"
        "--blocks;0"
        "--period-frames;0"
        "--channels;0"
        "--sample-rate;0"
        "--jitter-ms;1001"
        "--jitter-every;0"
        "--clock-offset-ppm;1001"
        "--clock-offset-ppm;-1001"
        "--clock-offset-ppm;invalid"
        "--unknown;1"
        "--blocks")
        execute_process(COMMAND "${PROBE}" ${arguments}
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT result STREQUAL "2" OR error STREQUAL "" OR NOT output STREQUAL "")
            message(FATAL_ERROR "Invalid options were not rejected cleanly: ${arguments}: ${result}: ${output}: ${error}")
        endif()
    endforeach()
elseif(MODE STREQUAL "null")
    execute_process(COMMAND "${PROBE}" --device null --blocks 10
        --clock-offset-ppm 100
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 10)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Probe failed: ${result}: ${output}: ${error}")
    endif()
    string(STRIP "${output}" output)
    foreach(field IN ITEMS rejected_blocks underruns missing_frames stream_failures)
        string(JSON value ERROR_VARIABLE json_error GET "${output}" "${field}")
        if(json_error OR NOT value STREQUAL "0")
            message(FATAL_ERROR "Invalid health field ${field}: ${output}")
        endif()
    endforeach()
    foreach(field IN ITEMS
        queued_frames_at_end queue_high_water_frames queue_capacity_frames
        current_rate_correction_ppm maximum_rate_correction_ppm)
        string(JSON value ERROR_VARIABLE json_error GET "${output}" "${field}")
        if(json_error)
            message(FATAL_ERROR "Missing queue metric ${field}: ${output}")
        endif()
    endforeach()
    string(JSON queued_end GET "${output}" queued_frames_at_end)
    string(JSON queue_high_water GET "${output}" queue_high_water_frames)
    string(JSON queue_capacity GET "${output}" queue_capacity_frames)
    string(JSON current_rate_correction GET "${output}" current_rate_correction_ppm)
    string(JSON maximum_rate_correction GET "${output}" maximum_rate_correction_ppm)
    if(queue_capacity LESS_EQUAL 0 OR queued_end GREATER queue_capacity
       OR queue_high_water LESS_EQUAL 0 OR queue_high_water GREATER queue_capacity)
        message(FATAL_ERROR "Invalid queue occupancy metrics: ${output}")
    endif()
    if(current_rate_correction LESS -500 OR current_rate_correction GREATER 500
       OR maximum_rate_correction GREATER 500)
        message(FATAL_ERROR "Rate correction exceeded its +/-500 ppm bound: ${output}")
    endif()
    foreach(pair IN ITEMS
        "blocks;10" "channels;1" "backend;alsa" "clock_offset_ppm;100")
        list(GET pair 0 field)
        list(GET pair 1 expected)
        string(JSON value GET "${output}" "${field}")
        if(NOT value STREQUAL expected)
            message(FATAL_ERROR "Unexpected ${field}: ${output}")
        endif()
    endforeach()
    string(JSON platform GET "${output}" platform)
    if(NOT platform MATCHES "^(std|evl)$")
        message(FATAL_ERROR "Unexpected platform: ${output}")
    endif()
else()
    message(FATAL_ERROR "Unknown probe test mode: ${MODE}")
endif()
