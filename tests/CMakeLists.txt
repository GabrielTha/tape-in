# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors

add_executable(tapein_tests
    unit/version_test.cpp
    unit/result_test.cpp
    unit/byte_reader_test.cpp
    unit/base64_test.cpp
    unit/rtsp_message_test.cpp
    unit/rtsp_parser_test.cpp
    unit/sdp_test.cpp
    unit/crypto_test.cpp
    unit/alac_decoder_test.cpp
    unit/rtp_packets_test.cpp
    unit/jitter_buffer_test.cpp
    unit/wav_writer_test.cpp
    unit/socket_test.cpp
    unit/reactor_test.cpp
    unit/receiver_test.cpp
    unit/dns_sd_advertiser_test.cpp
    unit/cli_args_test.cpp
    unit/wav_sink_test.cpp
    unit/capture_writer_test.cpp
    unit/resampler_test.cpp
)
target_link_libraries(tapein_tests PRIVATE tapein::core tapein::platform tapein::cli_lib tapein::warnings
    Catch2::Catch2WithMain alac::encoder MbedTLS::mbedcrypto SampleRate::samplerate)

include(Catch)
catch_discover_tests(tapein_tests)

add_subdirectory(fuzz)

if(TAPEIN_INTEGRATION AND TAPEIN_BUILD_TOOLS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if(TAPEIN_ATVREMOTE STREQUAL "")
        message(FATAL_ERROR "TAPEIN_INTEGRATION needs -DTAPEIN_ATVREMOTE=<path to atvremote>")
    endif()
    add_test(NAME pyatv_integration
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/integration/pyatv_stream_test.py
                --cli $<TARGET_FILE:tapein-cli> --atvremote ${TAPEIN_ATVREMOTE} --seconds 15)
    set_tests_properties(pyatv_integration PROPERTIES TIMEOUT 180 LABELS integration)
endif()
