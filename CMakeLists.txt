cmake_minimum_required(VERSION 3.15)
project(MusicRaT VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MUSICRAT_SAMPLE_TYPE "float" CACHE STRING "Audio sample type (float or double)")
set_property(CACHE MUSICRAT_SAMPLE_TYPE PROPERTY STRINGS float double)
set(MUSICRAT_MAX_AUDIO_CHANNELS "8" CACHE STRING "Maximum channels in an audio block")
set(MUSICRAT_MAX_AUDIO_FRAMES "4096" CACHE STRING "Maximum frames per audio block")
set(MUSICRAT_MAX_PARAMETER_EVENTS "256" CACHE STRING "Maximum parameter events in one block")
set(MUSICRAT_FILE_PROXY_BYTES "262144" CACHE STRING "CoreRaT file-proxy buffer size in bytes")
set(MUSICRAT_DEFAULT_SAMPLE_RATE_HZ "48000.0" CACHE STRING "Default sample rate in Hz")
set(MUSICRAT_DEFAULT_BLOCK_PERIOD_MS "10" CACHE STRING "Default processing period in milliseconds")

if(NOT MUSICRAT_SAMPLE_TYPE MATCHES "^(float|double)$")
    message(FATAL_ERROR "MUSICRAT_SAMPLE_TYPE must be float or double")
endif()

foreach(setting MUSICRAT_MAX_AUDIO_CHANNELS MUSICRAT_MAX_AUDIO_FRAMES MUSICRAT_MAX_PARAMETER_EVENTS MUSICRAT_FILE_PROXY_BYTES MUSICRAT_DEFAULT_BLOCK_PERIOD_MS)
    if(NOT ${setting} MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "${setting} must be a positive integer")
    endif()
endforeach()

if(MUSICRAT_MAX_AUDIO_CHANNELS GREATER 65535)
    message(FATAL_ERROR "MUSICRAT_MAX_AUDIO_CHANNELS must fit in uint16_t")
endif()

if(MUSICRAT_MAX_AUDIO_FRAMES GREATER 4294967295)
    message(FATAL_ERROR "MUSICRAT_MAX_AUDIO_FRAMES must fit in uint32_t")
endif()

math(EXPR MUSICRAT_MAX_PCM16_BLOCK_BYTES
    "${MUSICRAT_MAX_AUDIO_CHANNELS} * ${MUSICRAT_MAX_AUDIO_FRAMES} * 2")
if(MUSICRAT_FILE_PROXY_BYTES LESS MUSICRAT_MAX_PCM16_BLOCK_BYTES)
    message(FATAL_ERROR
        "MUSICRAT_FILE_PROXY_BYTES must hold one maximum-size interleaved PCM16 block (${MUSICRAT_MAX_PCM16_BLOCK_BYTES} bytes)")
endif()

if(NOT MUSICRAT_DEFAULT_SAMPLE_RATE_HZ MATCHES "^[0-9]+([.][0-9]+)?$")
    message(FATAL_ERROR "MUSICRAT_DEFAULT_SAMPLE_RATE_HZ must be a positive number")
endif()
if(MUSICRAT_DEFAULT_SAMPLE_RATE_HZ LESS_EQUAL 0)
    message(FATAL_ERROR "MUSICRAT_DEFAULT_SAMPLE_RATE_HZ must be greater than zero")
endif()

set(MUSICRAT_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated")
configure_file(
    include/musicrat/config.hpp.in
    "${MUSICRAT_GENERATED_INCLUDE_DIR}/musicrat/config.hpp"
    @ONLY
)

message(STATUS "MusicRaT audio policy: ${MUSICRAT_SAMPLE_TYPE}, ${MUSICRAT_MAX_AUDIO_CHANNELS} channels, ${MUSICRAT_MAX_AUDIO_FRAMES} frames, ${MUSICRAT_MAX_PARAMETER_EVENTS} parameter events, ${MUSICRAT_FILE_PROXY_BYTES} file-proxy bytes, ${MUSICRAT_DEFAULT_SAMPLE_RATE_HZ} Hz, ${MUSICRAT_DEFAULT_BLOCK_PERIOD_MS} ms")

# Set CMake prefix path to find packages
list(APPEND CMAKE_PREFIX_PATH /usr/local/lib/cmake/CommRaT)
list(APPEND CMAKE_PREFIX_PATH /usr/local/lib/cmake/SeRTial)

# Find required packages
find_package(CoreRaT REQUIRED)
find_package(CommRaT REQUIRED)
find_package(SeRTial REQUIRED)

set(MUSICRAT_FLAC_SUPPORT "AUTO" CACHE STRING
    "FLAC decoder support (AUTO, ON, or OFF)")
set_property(CACHE MUSICRAT_FLAC_SUPPORT PROPERTY STRINGS AUTO ON OFF)
if(NOT MUSICRAT_FLAC_SUPPORT MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "MUSICRAT_FLAC_SUPPORT must be AUTO, ON, or OFF")
endif()

set(MUSICRAT_HAS_FLAC OFF)
if(NOT MUSICRAT_FLAC_SUPPORT STREQUAL "OFF")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBFLAC QUIET IMPORTED_TARGET flac)
    endif()
    if(TARGET PkgConfig::LIBFLAC)
        set(MUSICRAT_HAS_FLAC ON)
    elseif(MUSICRAT_FLAC_SUPPORT STREQUAL "ON")
        message(FATAL_ERROR
            "MUSICRAT_FLAC_SUPPORT=ON requires libFLAC development files")
    endif()
endif()

set(MUSICRAT_MP3_SUPPORT "AUTO" CACHE STRING
    "MP3 decoder support (AUTO, ON, or OFF)")
set_property(CACHE MUSICRAT_MP3_SUPPORT PROPERTY STRINGS AUTO ON OFF)
if(NOT MUSICRAT_MP3_SUPPORT MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "MUSICRAT_MP3_SUPPORT must be AUTO, ON, or OFF")
endif()

set(MUSICRAT_HAS_MP3 OFF)
if(NOT MUSICRAT_MP3_SUPPORT STREQUAL "OFF")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBMPG123 QUIET IMPORTED_TARGET libmpg123)
    endif()
    if(TARGET PkgConfig::LIBMPG123)
        set(MUSICRAT_HAS_MP3 ON)
    elseif(MUSICRAT_MP3_SUPPORT STREQUAL "ON")
        message(FATAL_ERROR
            "MUSICRAT_MP3_SUPPORT=ON requires libmpg123 development files")
    endif()
endif()

set(MUSICRAT_RUBBERBAND_SUPPORT "AUTO" CACHE STRING
    "Rubber Band pitch-lock support (AUTO, ON, or OFF)")
set_property(CACHE MUSICRAT_RUBBERBAND_SUPPORT PROPERTY STRINGS AUTO ON OFF)
if(NOT MUSICRAT_RUBBERBAND_SUPPORT MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "MUSICRAT_RUBBERBAND_SUPPORT must be AUTO, ON, or OFF")
endif()

set(MUSICRAT_HAS_RUBBERBAND OFF)
if(NOT MUSICRAT_RUBBERBAND_SUPPORT STREQUAL "OFF")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(RUBBERBAND QUIET IMPORTED_TARGET rubberband>=3.0)
    endif()
    if(TARGET PkgConfig::RUBBERBAND)
        set(MUSICRAT_HAS_RUBBERBAND ON)
    elseif(MUSICRAT_RUBBERBAND_SUPPORT STREQUAL "ON")
        message(FATAL_ERROR
            "MUSICRAT_RUBBERBAND_SUPPORT=ON requires Rubber Band 3 development files")
    endif()
endif()

include(CheckIncludeFileCXX)
set(CMAKE_REQUIRED_LIBRARIES CoreRaT::corerat)
check_include_file_cxx("corerat/platform/file.hpp" MUSICRAT_HAS_CORERAT_FILE)
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT MUSICRAT_HAS_CORERAT_FILE)
    message(FATAL_ERROR
        "The selected CoreRaT package does not provide corerat/platform/file.hpp. "
        "Install a current CoreRaT build with the real-time file proxy API.")
endif()

add_library(musicrat_core INTERFACE)
target_include_directories(musicrat_core INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${MUSICRAT_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(musicrat_core INTERFACE
    CoreRaT::corerat
    CommRaT::commrat
    SeRTial::sertial
)
if(MUSICRAT_HAS_FLAC)
    target_link_libraries(musicrat_core INTERFACE PkgConfig::LIBFLAC)
    target_compile_definitions(musicrat_core INTERFACE MUSICRAT_HAS_FLAC=1)
    message(STATUS "MusicRaT FLAC decoder: enabled")
else()
    target_compile_definitions(musicrat_core INTERFACE MUSICRAT_HAS_FLAC=0)
    message(STATUS "MusicRaT FLAC decoder: disabled")
endif()
if(MUSICRAT_HAS_MP3)
    target_link_libraries(musicrat_core INTERFACE PkgConfig::LIBMPG123)
    target_compile_definitions(musicrat_core INTERFACE MUSICRAT_HAS_MP3=1)
    message(STATUS "MusicRaT MP3 decoder: enabled")
else()
    target_compile_definitions(musicrat_core INTERFACE MUSICRAT_HAS_MP3=0)
    message(STATUS "MusicRaT MP3 decoder: disabled")
endif()
if(MUSICRAT_HAS_RUBBERBAND)
    target_link_libraries(musicrat_core INTERFACE PkgConfig::RUBBERBAND)
    target_compile_definitions(musicrat_core INTERFACE MUSICRAT_HAS_RUBBERBAND=1)
    message(STATUS "MusicRaT Rubber Band pitch lock: enabled")
else()
    target_compile_definitions(musicrat_core INTERFACE MUSICRAT_HAS_RUBBERBAND=0)
    message(STATUS "MusicRaT Rubber Band pitch lock: disabled")
endif()

# Build options
option(BUILD_MUSICRAT "Build MusicRaT synthesizer" OFF)
option(BUILD_MUSICXML "Build MusicXML support" OFF)

if(BUILD_MUSICRAT)
    message(STATUS "Building MusicRaT")
    if(NOT TARGET commrat)
        add_library(commrat ALIAS CommRaT::commrat)
    endif()

    commrat_module(musicrat_oscillator
        SOURCES src/modules/oscillator_main.cpp
        MODULE_CLASS MusicRaTSineOscillator
        LINKS
        musicrat_core
    )
    target_compile_definitions(musicrat_oscillator PRIVATE BUILD_MUSICRAT)

    commrat_module(musicrat_parameter_source
        SOURCES src/modules/parameter_source_main.cpp
        MODULE_CLASS MusicRaTParameterSource
        LINKS
        musicrat_core
    )

    commrat_module(musicrat_deck_control_source
        SOURCES src/modules/deck_control_source_main.cpp
        MODULE_CLASS MusicRaTDeckControlSource
        LINKS
        musicrat_core
    )

    commrat_module(musicrat_transport_source
        SOURCES src/modules/transport_source_main.cpp
        MODULE_CLASS MusicRaTTransportSource
        LINKS
        musicrat_core
    )

    commrat_module(musicrat_gain
        SOURCES src/modules/gain_main.cpp
        MODULE_CLASS MusicRaTGain
        LINKS
        musicrat_core
    )

    commrat_module(musicrat_null_sink
        SOURCES src/modules/null_sink_main.cpp
        MODULE_CLASS MusicRaTNullSink
        LINKS
        musicrat_core
    )

    commrat_module(musicrat_wav_sink
        SOURCES src/modules/wav_sink_main.cpp
        MODULE_CLASS MusicRaTWavSink
        LINKS
        musicrat_core
    )

    commrat_module(musicrat_audio_file_player
        SOURCES src/modules/audio_file_player_main.cpp
        MODULE_CLASS MusicRaTAudioFilePlayer
        LINKS
        musicrat_core
    )

    commrat_module(musicrat_level_meter
        SOURCES src/modules/level_meter_main.cpp
        MODULE_CLASS MusicRaTLevelMeter
        LINKS
        musicrat_core
    )

    commrat_module(musicrat_telemetry_sink
        SOURCES src/modules/telemetry_sink_main.cpp
        MODULE_CLASS MusicRaTTelemetrySink
        LINKS
        musicrat_core
    )

    add_executable(musicrat_launcher src/apps/launcher_main.cpp)
    target_link_libraries(musicrat_launcher PRIVATE musicrat_core)

    # Compiler warnings
    if(MSVC)
        target_compile_options(musicrat_oscillator PRIVATE /W4)
        target_compile_options(musicrat_parameter_source PRIVATE /W4)
        target_compile_options(musicrat_deck_control_source PRIVATE /W4)
        target_compile_options(musicrat_gain PRIVATE /W4)
        target_compile_options(musicrat_null_sink PRIVATE /W4)
        target_compile_options(musicrat_wav_sink PRIVATE /W4)
        target_compile_options(musicrat_audio_file_player PRIVATE /W4)
        target_compile_options(musicrat_level_meter PRIVATE /W4)
        target_compile_options(musicrat_telemetry_sink PRIVATE /W4)
        target_compile_options(musicrat_launcher PRIVATE /W4)
    else()
        target_compile_options(musicrat_oscillator PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(musicrat_parameter_source PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(musicrat_deck_control_source PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(musicrat_gain PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(musicrat_null_sink PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(musicrat_wav_sink PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(musicrat_audio_file_player PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(musicrat_level_meter PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(musicrat_telemetry_sink PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(musicrat_launcher PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    install(TARGETS
        musicrat_oscillator
        musicrat_parameter_source
        musicrat_deck_control_source
        musicrat_gain
        musicrat_null_sink
        musicrat_wav_sink
        musicrat_audio_file_player
        musicrat_level_meter
        musicrat_telemetry_sink
        musicrat_launcher
        DESTINATION bin)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(musicrat_audio_block_test tests/protocol/audio_block_test.cpp)
    target_link_libraries(musicrat_audio_block_test PRIVATE musicrat_core)
    add_test(NAME musicrat.protocol.audio_block COMMAND musicrat_audio_block_test)

    add_executable(musicrat_transport_test tests/protocol/transport_test.cpp)
    target_link_libraries(musicrat_transport_test PRIVATE musicrat_core)
    add_test(NAME musicrat.protocol.transport COMMAND musicrat_transport_test)

    add_executable(musicrat_parameter_event_block_test tests/protocol/parameter_event_block_test.cpp)
    target_link_libraries(musicrat_parameter_event_block_test PRIVATE musicrat_core)
    add_test(NAME musicrat.protocol.parameter_event_block COMMAND musicrat_parameter_event_block_test)

    add_executable(musicrat_gain_test tests/dsp/gain_test.cpp)
    target_link_libraries(musicrat_gain_test PRIVATE musicrat_core)
    add_test(NAME musicrat.dsp.gain COMMAND musicrat_gain_test)

    add_executable(musicrat_gain_processor_test tests/dsp/gain_processor_test.cpp)
    target_link_libraries(musicrat_gain_processor_test PRIVATE musicrat_core)
    add_test(NAME musicrat.dsp.gain_processor COMMAND musicrat_gain_processor_test)

    add_executable(musicrat_varispeed_renderer_test
        tests/dsp/varispeed_renderer_test.cpp)
    target_link_libraries(musicrat_varispeed_renderer_test PRIVATE musicrat_core)
    add_test(NAME musicrat.dsp.varispeed_renderer COMMAND musicrat_varispeed_renderer_test)

    add_executable(musicrat_beat_grid_test tests/dsp/beat_grid_test.cpp)
    target_link_libraries(musicrat_beat_grid_test PRIVATE musicrat_core)
    add_test(NAME musicrat.dsp.beat_grid COMMAND musicrat_beat_grid_test)

    add_executable(musicrat_transport_follower_test
        tests/dsp/transport_follower_test.cpp)
    target_link_libraries(musicrat_transport_follower_test PRIVATE musicrat_core)
    add_test(NAME musicrat.dsp.transport_follower
        COMMAND musicrat_transport_follower_test)

    add_executable(musicrat_deck_control_quantizer_test
        tests/dsp/deck_control_quantizer_test.cpp)
    target_link_libraries(musicrat_deck_control_quantizer_test PRIVATE musicrat_core)
    add_test(NAME musicrat.dsp.deck_control_quantizer
        COMMAND musicrat_deck_control_quantizer_test)

    if(MUSICRAT_HAS_RUBBERBAND)
        add_executable(musicrat_pitch_lock_processor_test
            tests/dsp/pitch_lock_processor_test.cpp)
        target_link_libraries(musicrat_pitch_lock_processor_test PRIVATE musicrat_core)
        add_test(NAME musicrat.dsp.pitch_lock_processor
            COMMAND musicrat_pitch_lock_processor_test)
    endif()

    add_executable(musicrat_level_meter_block_test tests/protocol/level_meter_block_test.cpp)
    target_link_libraries(musicrat_level_meter_block_test PRIVATE musicrat_core)
    add_test(NAME musicrat.protocol.level_meter_block COMMAND musicrat_level_meter_block_test)

    add_executable(musicrat_playback_status_block_test
        tests/protocol/playback_status_block_test.cpp)
    target_link_libraries(musicrat_playback_status_block_test PRIVATE musicrat_core)
    add_test(NAME musicrat.protocol.playback_status_block
        COMMAND musicrat_playback_status_block_test)

    add_executable(musicrat_level_meter_processor_test tests/dsp/level_meter_processor_test.cpp)
    target_link_libraries(musicrat_level_meter_processor_test PRIVATE musicrat_core)
    add_test(NAME musicrat.dsp.level_meter_processor COMMAND musicrat_level_meter_processor_test)

    add_executable(musicrat_wav_writer_test tests/backends/wav_writer_test.cpp)
    target_link_libraries(musicrat_wav_writer_test PRIVATE musicrat_core)
    add_test(NAME musicrat.backends.wav_writer COMMAND musicrat_wav_writer_test)

    add_executable(musicrat_wav_reader_test tests/backends/wav_reader_test.cpp)
    target_link_libraries(musicrat_wav_reader_test PRIVATE musicrat_core)
    add_test(NAME musicrat.backends.wav_reader COMMAND musicrat_wav_reader_test)

    if(MUSICRAT_HAS_FLAC)
        add_executable(musicrat_flac_reader_test
            tests/backends/flac_reader_test.cpp)
        target_include_directories(musicrat_flac_reader_test PRIVATE tests/support)
        target_link_libraries(musicrat_flac_reader_test PRIVATE musicrat_core)
        add_test(NAME musicrat.backends.flac_reader
            COMMAND musicrat_flac_reader_test)
    endif()

    if(MUSICRAT_HAS_MP3)
        add_executable(musicrat_mp3_reader_test
            tests/backends/mp3_reader_test.cpp)
        target_include_directories(musicrat_mp3_reader_test PRIVATE tests/support)
        target_link_libraries(musicrat_mp3_reader_test PRIVATE musicrat_core)
        add_test(NAME musicrat.backends.mp3_reader
            COMMAND musicrat_mp3_reader_test)
    endif()

    add_executable(musicrat_media_decoder_test
        tests/backends/media_decoder_test.cpp)
    if(MUSICRAT_HAS_FLAC OR MUSICRAT_HAS_MP3)
        target_include_directories(musicrat_media_decoder_test PRIVATE tests/support)
    endif()
    target_link_libraries(musicrat_media_decoder_test PRIVATE musicrat_core)
    add_test(NAME musicrat.backends.media_decoder
        COMMAND musicrat_media_decoder_test)

    add_executable(musicrat_decoded_audio_pool_test
        tests/backends/decoded_audio_pool_test.cpp)
    target_link_libraries(musicrat_decoded_audio_pool_test PRIVATE musicrat_core)
    add_test(NAME musicrat.backends.decoded_audio_pool
        COMMAND musicrat_decoded_audio_pool_test)

    add_executable(musicrat_decode_ahead_test
        tests/backends/decode_ahead_test.cpp)
    target_link_libraries(musicrat_decode_ahead_test PRIVATE musicrat_core)
    add_test(NAME musicrat.backends.decode_ahead
        COMMAND musicrat_decode_ahead_test)

    add_executable(musicrat_wav_decode_ahead_test
        tests/backends/wav_decode_ahead_test.cpp)
    target_link_libraries(musicrat_wav_decode_ahead_test PRIVATE musicrat_core)
    add_test(NAME musicrat.backends.wav_decode_ahead
        COMMAND musicrat_wav_decode_ahead_test)

    add_executable(musicrat_decode_ahead_renderer_test
        tests/backends/decode_ahead_renderer_test.cpp)
    target_link_libraries(musicrat_decode_ahead_renderer_test PRIVATE musicrat_core)
    add_test(NAME musicrat.backends.decode_ahead_renderer
        COMMAND musicrat_decode_ahead_renderer_test)

    add_executable(musicrat_wav_playback_coordinator_test
        tests/backends/wav_playback_coordinator_test.cpp)
    target_link_libraries(musicrat_wav_playback_coordinator_test PRIVATE musicrat_core)
    add_test(NAME musicrat.backends.wav_playback_coordinator
        COMMAND musicrat_wav_playback_coordinator_test)

    add_executable(musicrat_audio_format_validation_test
        tests/integration/audio_format_validation_test.cpp)
    target_link_libraries(musicrat_audio_format_validation_test PRIVATE musicrat_core)
    add_test(NAME musicrat.integration.audio_format_validation
        COMMAND musicrat_audio_format_validation_test)

    if(TARGET musicrat_launcher)
        add_test(
            NAME musicrat.integration.reject_incompatible_audio_format
            COMMAND ${CMAKE_COMMAND}
                "-DLAUNCHER=$<TARGET_FILE:musicrat_launcher>"
                "-DCONFIG=${PROJECT_SOURCE_DIR}/tests/integration/incompatible_audio_format.json"
                "-DEXPECTED_ERROR=Audio sample-rate mismatch"
                -P "${PROJECT_SOURCE_DIR}/tests/integration/expect_launcher_failure.cmake"
        )
            add_test(
                NAME musicrat.integration.reject_incompatible_passthrough_audio_format
                COMMAND ${CMAKE_COMMAND}
                "-DLAUNCHER=$<TARGET_FILE:musicrat_launcher>"
                "-DCONFIG=${PROJECT_SOURCE_DIR}/tests/integration/incompatible_passthrough_audio_format.json"
                "-DEXPECTED_ERROR=Audio sample-rate mismatch"
                -P "${PROJECT_SOURCE_DIR}/tests/integration/expect_launcher_failure.cmake"
            )

        find_program(MUSICRAT_CORERAT_ROUTER_TCP
            NAMES corerat-router-tcp
            REQUIRED)
        set(MUSICRAT_ROUTER_FIXTURE
            "${PROJECT_SOURCE_DIR}/tests/integration/tcp_router_fixture.sh")
        set(MUSICRAT_ROUTER_PID_FILE
            "${PROJECT_BINARY_DIR}/musicrat-test-router.pid")
        set(MUSICRAT_ROUTER_LOG_FILE
            "${PROJECT_BINARY_DIR}/musicrat-test-router.log")

        add_test(
            NAME musicrat.fixture.tcp_router.start
            COMMAND bash "${MUSICRAT_ROUTER_FIXTURE}"
                start
                "${MUSICRAT_CORERAT_ROUTER_TCP}"
                "${MUSICRAT_ROUTER_PID_FILE}"
                "${MUSICRAT_ROUTER_LOG_FILE}"
        )
        set_tests_properties(musicrat.fixture.tcp_router.start PROPERTIES
            FIXTURES_SETUP musicrat_tcp_router)

        add_test(
            NAME musicrat.fixture.tcp_router.stop
            COMMAND bash "${MUSICRAT_ROUTER_FIXTURE}"
                stop
                "${MUSICRAT_CORERAT_ROUTER_TCP}"
                "${MUSICRAT_ROUTER_PID_FILE}"
                "${MUSICRAT_ROUTER_LOG_FILE}"
        )
        set_tests_properties(musicrat.fixture.tcp_router.stop PROPERTIES
            FIXTURES_CLEANUP musicrat_tcp_router)

        add_executable(musicrat_null_sink_test tests/modules/null_sink_test.cpp)
        target_link_libraries(musicrat_null_sink_test PRIVATE musicrat_core)
        add_test(NAME musicrat.modules.null_sink COMMAND musicrat_null_sink_test)

        add_executable(musicrat_wav_sink_test tests/modules/wav_sink_test.cpp)
        target_link_libraries(musicrat_wav_sink_test PRIVATE musicrat_core)
        add_test(NAME musicrat.modules.wav_sink COMMAND musicrat_wav_sink_test)

        add_executable(musicrat_audio_file_player_test
            tests/modules/audio_file_player_test.cpp)
        target_link_libraries(musicrat_audio_file_player_test PRIVATE musicrat_core)
        add_test(NAME musicrat.modules.audio_file_player
            COMMAND musicrat_audio_file_player_test)

        add_executable(musicrat_transport_source_test
            tests/modules/transport_source_test.cpp)
        target_link_libraries(musicrat_transport_source_test PRIVATE musicrat_core)
        add_test(NAME musicrat.modules.transport_source
            COMMAND musicrat_transport_source_test)

        add_test(
            NAME musicrat.integration.controlled_gain
            COMMAND musicrat_launcher
                "${PROJECT_SOURCE_DIR}/examples/configs/controlled_gain.json"
                --duration-ms 300
        )

        add_executable(musicrat_tone_to_wav_test
            tests/integration/tone_to_wav_test.cpp)
        add_test(
            NAME musicrat.integration.tone_to_wav
            COMMAND musicrat_tone_to_wav_test
                "$<TARGET_FILE:musicrat_launcher>"
                "${PROJECT_SOURCE_DIR}/examples/configs/tone_to_wav.json"
        )
            add_test(
                NAME musicrat.integration.tone_through_processors_to_wav
                COMMAND musicrat_tone_to_wav_test
                "$<TARGET_FILE:musicrat_launcher>"
                "${PROJECT_SOURCE_DIR}/examples/configs/tone_through_processors_to_wav.json"
            )
        add_executable(musicrat_audio_file_player_to_wav_test
            tests/integration/audio_file_player_to_wav_test.cpp)
        target_link_libraries(musicrat_audio_file_player_to_wav_test PRIVATE musicrat_core)
        add_test(
            NAME musicrat.integration.audio_file_player_to_wav
            COMMAND musicrat_audio_file_player_to_wav_test
                "$<TARGET_FILE:musicrat_launcher>"
                "${PROJECT_SOURCE_DIR}/examples/configs/audio_file_player_to_wav.json"
        )
        add_test(
            NAME musicrat.integration.audio_file_player_transport_sync_to_wav
            COMMAND musicrat_audio_file_player_to_wav_test
                "$<TARGET_FILE:musicrat_launcher>"
                "${PROJECT_SOURCE_DIR}/examples/configs/audio_file_player_transport_sync_to_wav.json"
        )
            add_test(
                NAME musicrat.integration.audio_file_player_quantized_start_to_wav
                COMMAND musicrat_audio_file_player_to_wav_test
                "$<TARGET_FILE:musicrat_launcher>"
                "${PROJECT_SOURCE_DIR}/examples/configs/audio_file_player_quantized_start_to_wav.json"
            )
        if(MUSICRAT_HAS_RUBBERBAND)
            add_test(
                NAME musicrat.integration.audio_file_player_pitch_lock_to_wav
                COMMAND musicrat_audio_file_player_to_wav_test
                    "$<TARGET_FILE:musicrat_launcher>"
                    "${PROJECT_SOURCE_DIR}/examples/configs/audio_file_player_pitch_lock_to_wav.json"
            )
        endif()
        if(MUSICRAT_HAS_FLAC)
            add_executable(musicrat_audio_file_player_flac_to_wav_test
                tests/integration/audio_file_player_flac_to_wav_test.cpp)
            target_include_directories(
                musicrat_audio_file_player_flac_to_wav_test PRIVATE tests/support)
            target_link_libraries(
                musicrat_audio_file_player_flac_to_wav_test PRIVATE musicrat_core)
            add_test(
                NAME musicrat.integration.audio_file_player_flac_to_wav
                COMMAND musicrat_audio_file_player_flac_to_wav_test
                    "$<TARGET_FILE:musicrat_launcher>"
                    "${PROJECT_SOURCE_DIR}/examples/configs/audio_file_player_flac_to_wav.json"
            )
        endif()
        if(MUSICRAT_HAS_MP3)
            add_executable(musicrat_audio_file_player_mp3_to_wav_test
                tests/integration/audio_file_player_mp3_to_wav_test.cpp)
            target_include_directories(
                musicrat_audio_file_player_mp3_to_wav_test PRIVATE tests/support)
            target_link_libraries(
                musicrat_audio_file_player_mp3_to_wav_test PRIVATE musicrat_core)
            add_test(
                NAME musicrat.integration.audio_file_player_mp3_to_wav
                COMMAND musicrat_audio_file_player_mp3_to_wav_test
                    "$<TARGET_FILE:musicrat_launcher>"
                    "${PROJECT_SOURCE_DIR}/examples/configs/audio_file_player_mp3_to_wav.json"
            )
        endif()
        if(MUSICRAT_HAS_RUBBERBAND)
            set_tests_properties(
                musicrat.integration.audio_file_player_pitch_lock_to_wav
                PROPERTIES
                    FIXTURES_REQUIRED musicrat_tcp_router
                    RESOURCE_LOCK musicrat_tcp_router)
        endif()
        set_tests_properties(
            musicrat.integration.controlled_gain
            musicrat.integration.tone_to_wav
            musicrat.integration.tone_through_processors_to_wav
            musicrat.integration.audio_file_player_to_wav
            musicrat.integration.audio_file_player_transport_sync_to_wav
            musicrat.integration.audio_file_player_quantized_start_to_wav
            PROPERTIES
                FIXTURES_REQUIRED musicrat_tcp_router
                RESOURCE_LOCK musicrat_tcp_router)
        if(MUSICRAT_HAS_FLAC)
            set_tests_properties(
                musicrat.integration.audio_file_player_flac_to_wav
                PROPERTIES
                    FIXTURES_REQUIRED musicrat_tcp_router
                    RESOURCE_LOCK musicrat_tcp_router)
        endif()
        if(MUSICRAT_HAS_MP3)
            set_tests_properties(
                musicrat.integration.audio_file_player_mp3_to_wav
                PROPERTIES
                    FIXTURES_REQUIRED musicrat_tcp_router
                    RESOURCE_LOCK musicrat_tcp_router)
        endif()
    endif()
endif()

if(BUILD_MUSICXML)
    message(STATUS "Building MusicXML support")
    # Create musicxml executable
    add_executable(musicxml src/apps/musicxml_main.cpp)
    target_compile_definitions(musicxml PRIVATE BUILD_MUSICXML)

    # Link libraries
    target_link_libraries(musicxml PUBLIC
        CommRaT::commrat
        SeRTial::sertial
        pthread
        rt
    )

    # Compiler warnings
    if(MSVC)
        target_compile_options(musicxml PRIVATE /W4)
    else()
        target_compile_options(musicxml PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    install(TARGETS musicxml DESTINATION bin)
endif()