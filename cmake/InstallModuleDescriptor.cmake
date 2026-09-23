if(NOT EXISTS "${MUSICRAT_DESCRIPTOR_SOURCE}")
    message(FATAL_ERROR
        "Module descriptor does not exist: ${MUSICRAT_DESCRIPTOR_SOURCE}")
endif()

file(READ "${MUSICRAT_DESCRIPTOR_SOURCE}" descriptor_json)
set(installed_binary
    "${CMAKE_INSTALL_PREFIX}/${MUSICRAT_DESCRIPTOR_BINARY}")
string(REPLACE "\\" "\\\\" installed_binary_json "${installed_binary}")
string(REPLACE "\"" "\\\"" installed_binary_json "${installed_binary_json}")
string(REGEX REPLACE
    "\"binary\":\"[^\"]*\""
    "\"binary\":\"${installed_binary_json}\""
    installed_descriptor_json
    "${descriptor_json}")

set(installed_descriptor
    "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${MUSICRAT_DESCRIPTOR_DESTINATION}")
get_filename_component(installed_descriptor_directory
    "${installed_descriptor}" DIRECTORY)
file(MAKE_DIRECTORY "${installed_descriptor_directory}")
file(WRITE "${installed_descriptor}" "${installed_descriptor_json}")
message(STATUS "Installing: ${installed_descriptor}")