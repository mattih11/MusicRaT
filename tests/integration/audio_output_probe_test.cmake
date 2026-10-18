if(MODE STREQUAL "invalid")
    foreach(arguments IN ITEMS
        "--blocks;invalid"
        "--blocks;0"
        "--period-frames;0"
        "--channels;0"
        "--sample-rate;0"
        "--jitter-ms;1001"
        "--jitter-every;0"
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
    foreach(pair IN ITEMS "blocks;10" "channels;1" "backend;alsa")
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
