if(NOT DEFINED BUILD_DIR OR NOT DEFINED INSTALL_PREFIX
   OR NOT DEFINED INSTALL_BINDIR OR NOT DEFINED INSTALL_DATADIR)
    message(FATAL_ERROR
        "BUILD_DIR, INSTALL_PREFIX, INSTALL_BINDIR, and INSTALL_DATADIR are required")
endif()

file(REMOVE_RECURSE "${INSTALL_PREFIX}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR
        "MusicRaT temporary install failed:\n${install_output}\n${install_error}")
endif()

file(GLOB descriptors
    "${INSTALL_PREFIX}/${INSTALL_DATADIR}/musicrat/modules/MusicRaT*.module.json")
list(LENGTH descriptors descriptor_count)
if(NOT descriptor_count EQUAL 11)
    message(FATAL_ERROR
        "Expected 11 installed module descriptors, found ${descriptor_count}")
endif()

foreach(descriptor IN LISTS descriptors)
    file(READ "${descriptor}" descriptor_json)
    string(REGEX MATCH "\"binary\":\"([^\"]+)\"" _ "${descriptor_json}")
    if(NOT CMAKE_MATCH_1)
        message(FATAL_ERROR "Installed descriptor has no binary path: ${descriptor}")
    endif()
    set(descriptor_binary "${CMAKE_MATCH_1}")
    if(NOT descriptor_binary MATCHES "^${INSTALL_PREFIX}/${INSTALL_BINDIR}/")
        message(FATAL_ERROR
            "Installed descriptor references a non-installed binary: ${descriptor_binary}")
    endif()
    if(NOT EXISTS "${descriptor_binary}")
        message(FATAL_ERROR
            "Installed descriptor binary does not exist: ${descriptor_binary}")
    endif()
endforeach()