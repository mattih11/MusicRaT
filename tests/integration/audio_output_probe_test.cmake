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
        "--blocks;8640001"
        "--blocks;100000;--jitter-ms;1000;--jitter-every;1"
        "--unknown;1"
        "--blocks")
        execute_process(COMMAND "${PROBE}" ${arguments}
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT result STREQUAL "2" OR error STREQUAL "" OR NOT output STREQUAL "")
            message(FATAL_ERROR "Invalid options were not rejected cleanly: ${arguments}: ${result}: ${output}: ${error}")
        endif()
    endforeach()
elseif(MODE STREQUAL "null")
    execute_process(COMMAND "${PROBE}" --device null --blocks 210
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
    string(JSON telemetry_interval GET "${output}" telemetry_interval_ns)
    string(JSON telemetry_count LENGTH "${output}" telemetry_samples)
    string(JSON telemetry_overflow GET "${output}" telemetry_overflow)
    if(NOT telemetry_interval STREQUAL "1000000000"
       OR telemetry_overflow
       OR telemetry_count LESS 2)
        message(FATAL_ERROR "Probe did not produce a bounded telemetry timeline: ${output}")
    endif()
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
    set(previous_elapsed -1)
    set(previous_block_sequence -1)
    set(previous_rejected_blocks 0)
    set(previous_underruns 0)
    set(previous_missing_frames 0)
    set(previous_stream_failures 0)
    math(EXPR last_sample "${telemetry_count} - 1")
    foreach(index RANGE 0 ${last_sample})
        string(JSON sample_elapsed GET "${output}" telemetry_samples ${index} elapsed_ns)
        string(JSON sample_block_sequence GET "${output}" telemetry_samples ${index} block_sequence)
        string(JSON sample_queue GET "${output}" telemetry_samples ${index} queued_frames)
        string(JSON sample_correction GET "${output}" telemetry_samples ${index} rate_correction_ppm)
        string(JSON sample_rejected GET "${output}" telemetry_samples ${index} rejected_blocks)
        string(JSON sample_underruns GET "${output}" telemetry_samples ${index} underruns)
        string(JSON sample_missing GET "${output}" telemetry_samples ${index} missing_frames)
        string(JSON sample_stream_failures GET "${output}" telemetry_samples ${index} stream_failures)
        if(sample_elapsed LESS_EQUAL previous_elapsed
           OR sample_block_sequence LESS_EQUAL previous_block_sequence
           OR sample_queue GREATER queue_capacity
           OR sample_correction LESS -500 OR sample_correction GREATER 500
           OR sample_rejected LESS previous_rejected_blocks
           OR sample_underruns LESS previous_underruns
           OR sample_missing LESS previous_missing_frames
           OR sample_stream_failures LESS previous_stream_failures)
            message(FATAL_ERROR "Probe telemetry sample was invalid or not monotonic: ${output}")
        endif()
        set(previous_elapsed "${sample_elapsed}")
        set(previous_block_sequence "${sample_block_sequence}")
        set(previous_rejected_blocks "${sample_rejected}")
        set(previous_underruns "${sample_underruns}")
        set(previous_missing_frames "${sample_missing}")
        set(previous_stream_failures "${sample_stream_failures}")
    endforeach()
    foreach(pair IN ITEMS
        "blocks;210" "channels;1" "backend;alsa" "clock_offset_ppm;100")
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
