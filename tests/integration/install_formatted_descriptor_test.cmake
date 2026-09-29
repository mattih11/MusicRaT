if(NOT DEFINED DESCRIPTOR_INSTALL_SCRIPT OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "DESCRIPTOR_INSTALL_SCRIPT and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

set(MUSICRAT_DESCRIPTOR_SOURCE "${TEST_ROOT}/source.module.json")
file(WRITE "${MUSICRAT_DESCRIPTOR_SOURCE}"
    "{\n  \"module_class\": \"MusicRaTTest\",\n  \"binary\": \"/tmp/build/musicrat_test\"\n}\n")

set(CMAKE_INSTALL_PREFIX "/usr")
set(MUSICRAT_DESCRIPTOR_BINARY "bin/musicrat_test")
set(MUSICRAT_DESCRIPTOR_DESTINATION
    "share/musicrat/modules/MusicRaTTest.module.json")
set(ENV{DESTDIR} "${TEST_ROOT}/stage")
include("${DESCRIPTOR_INSTALL_SCRIPT}")

set(installed_descriptor
    "${TEST_ROOT}/stage/usr/share/musicrat/modules/MusicRaTTest.module.json")
file(READ "${installed_descriptor}" descriptor_json)
if(NOT descriptor_json MATCHES "\"binary\":\"/usr/bin/musicrat_test\"")
    message(FATAL_ERROR
        "Installed descriptor has the wrong binary path: ${descriptor_json}")
endif()
if(descriptor_json MATCHES "/tmp/build")
    message(FATAL_ERROR
        "Installed descriptor retained its build path: ${descriptor_json}")
endif()