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
        "--backend-events;2"
        "--backend-events;invalid"
        "--wait-scheduling;1"
        "--wait-scheduling;2;--backend-events;1"
        "--wait-scheduling;invalid"
        "--unknown;1"
        "--blocks")
        execute_process(COMMAND "${PROBE}" ${arguments}
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT result STREQUAL "2" OR error STREQUAL "" OR NOT output STREQUAL "")
            message(FATAL_ERROR "Invalid options were not rejected cleanly: ${arguments}: ${result}: ${output}: ${error}")
        endif()
    endforeach()
elseif(MODE STREQUAL "null" OR MODE STREQUAL "events" OR MODE STREQUAL "scheduling")
    set(events 0)
    set(scheduling 0)
    if(NOT MODE STREQUAL "null")
        set(events 1)
    endif()
    if(MODE STREQUAL "scheduling")
        set(scheduling 1)
    endif()
    execute_process(COMMAND "${PROBE}" --device null --blocks 210
        --clock-offset-ppm 100 --backend-events "${events}"
        --wait-scheduling "${scheduling}"
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
    string(JSON diagnostics_type TYPE "${output}" backend_diagnostics)
    if(NOT events)
        if(NOT diagnostics_type STREQUAL "NULL")
            message(FATAL_ERROR "Backend diagnostics were not disabled by default")
        endif()
    else()
        string(JSON event_count LENGTH "${output}" backend_diagnostics events)
        string(JSON capacity GET "${output}" backend_diagnostics capacity)
        string(JSON dropped GET "${output}" backend_diagnostics dropped_events)
        string(JSON status_failures GET "${output}" backend_diagnostics status_failures)
        string(JSON scheduling_failures GET "${output}" backend_diagnostics scheduling_failures)
        string(JSON wait_scheduling GET "${output}" backend_diagnostics wait_scheduling)
        if((scheduling AND NOT wait_scheduling) OR (NOT scheduling AND wait_scheduling)
           OR NOT scheduling_failures EQUAL 0)
            message(FATAL_ERROR "Invalid scheduling capture metadata: ${output}")
        endif()
        string(JSON rate GET "${output}" backend_diagnostics device_sample_rate_hz)
        string(JSON period GET "${output}" backend_diagnostics device_period_frames)
        string(JSON buffer GET "${output}" backend_diagnostics device_buffer_frames)
        string(JSON start GET "${output}" backend_diagnostics start_timestamp_ns)
        string(JSON producer_start GET "${output}" producer_start_timestamp_ns)
        if(event_count LESS 2 OR event_count GREATER capacity
           OR NOT dropped EQUAL 0 OR NOT status_failures EQUAL 0
           OR NOT rate EQUAL 48000 OR period LESS_EQUAL 0 OR buffer LESS period
           OR start GREATER producer_start)
            message(FATAL_ERROR "Invalid backend capture metadata: ${output}")
        endif()
        set(previous_timestamp 0)
        set(previous_source 0)
        set(previous_device 0)
        math(EXPR last_event "${event_count} - 1")
        foreach(index RANGE 0 ${last_event})
            foreach(field IN ITEMS timestamp_ns observation_timestamp_ns reasons
                queued_before queued_after source_frames_consumed device_frames_written
                requested_frames write_result write_duration_ns wait_duration_ns
                worker_gap_ns correction_ppm wait_result recovery_result
                wait_start_timestamp_ns scheduling_before_timestamp_ns
                scheduling_after_timestamp_ns scheduling_runtime_ns
                scheduling_runnable_delay_ns scheduling_timeslices scheduling_result
                device_available_frames device_delay_frames device_state device_status_result)
                string(JSON ${field} GET "${output}" backend_diagnostics events ${index} ${field})
            endforeach()
            if(timestamp_ns LESS previous_timestamp
               OR observation_timestamp_ns LESS timestamp_ns OR reasons LESS_EQUAL 0
               OR queued_before GREATER queue_capacity OR queued_after GREATER queue_capacity
               OR source_frames_consumed LESS previous_source
               OR device_frames_written LESS previous_device)
                message(FATAL_ERROR "Invalid backend diagnostic event: ${output}")
            endif()
            if(scheduling AND write_result EQUAL -11)
                if(scheduling_result LESS 0
                   OR scheduling_before_timestamp_ns LESS timestamp_ns
                   OR scheduling_before_timestamp_ns GREATER wait_start_timestamp_ns
                   OR scheduling_after_timestamp_ns GREATER observation_timestamp_ns)
                    message(FATAL_ERROR "Invalid wait scheduling interval: ${output}")
                endif()
            endif()
            set(previous_timestamp "${timestamp_ns}")
            set(previous_source "${source_frames_consumed}")
            set(previous_device "${device_frames_written}")
        endforeach()
    endif()
else()
    message(FATAL_ERROR "Unknown probe test mode: ${MODE}")
endif()
