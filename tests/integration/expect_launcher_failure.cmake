execute_process(
    COMMAND "${LAUNCHER}" "${CONFIG}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)

if(result EQUAL 0)
    message(FATAL_ERROR "Launcher unexpectedly accepted invalid graph")
endif()

string(CONCAT combined "${output}" "${error}")
if(NOT combined MATCHES "${EXPECTED_ERROR}")
    message(FATAL_ERROR
        "Launcher failed without expected diagnostic '${EXPECTED_ERROR}':\n${combined}")
endif()