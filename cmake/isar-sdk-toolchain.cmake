if(DEFINED ENV{EVL_SDK_DIR})
    if(IS_ABSOLUTE "$ENV{EVL_SDK_DIR}")
        set(_EVL_SDK "$ENV{EVL_SDK_DIR}" CACHE PATH "RaTOS ISAR SDK root" FORCE)
    else()
        set(_EVL_SDK "${CMAKE_SOURCE_DIR}/$ENV{EVL_SDK_DIR}" CACHE PATH
            "RaTOS ISAR SDK root" FORCE)
    endif()
elseif(NOT DEFINED CACHE{_EVL_SDK})
    message(FATAL_ERROR
        "EVL_SDK_DIR is not set. Export the path to an extracted RaTOS SDK before configuring.")
endif()

if(EXISTS "${_EVL_SDK}/usr/bin/x86_64-linux-gnu-g++-14.bin")
    set(CMAKE_C_COMPILER
        "${_EVL_SDK}/usr/bin/x86_64-linux-gnu-gcc-14.bin"
        CACHE FILEPATH "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER
        "${_EVL_SDK}/usr/bin/x86_64-linux-gnu-g++-14.bin"
        CACHE FILEPATH "C++ compiler" FORCE)
    set(_EVL_SYSROOT_FLAG "--sysroot=${_EVL_SDK}")
else()
    set(CMAKE_C_COMPILER
        "${_EVL_SDK}/usr/bin/x86_64-linux-gnu-gcc"
        CACHE FILEPATH "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER
        "${_EVL_SDK}/usr/bin/x86_64-linux-gnu-g++"
        CACHE FILEPATH "C++ compiler" FORCE)
    set(_EVL_SYSROOT_FLAG "")
endif()

foreach(language C CXX)
    set(CMAKE_${language}_FLAGS_INIT "${_EVL_SYSROOT_FLAG}"
        CACHE STRING "Initial ${language} flags" FORCE)
endforeach()

set(CMAKE_FIND_ROOT_PATH "${_EVL_SDK}")
list(PREPEND CMAKE_PREFIX_PATH "${_EVL_SDK}/usr")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
