# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# tapein_core: pure C++20 protocol and audio core. No JUCE, no Objective-C.

add_library(tapein_core STATIC
    src/version.cpp
    src/util/base64.cpp
    src/rtsp/rtsp_message.cpp
    src/rtsp/rtsp_parser.cpp
    src/rtsp/sdp.cpp
    src/crypto/raop_key.cpp
    src/crypto/apple_challenge.cpp
    src/crypto/aes_cbc.cpp
    src/codec/alac_decoder.cpp
    src/rtp/rtp_packets.cpp
    src/buffer/jitter_buffer.cpp
    src/util/wav_writer.cpp
    src/net/socket.cpp
    src/net/reactor.cpp
    src/session/rtsp_connection.cpp
    src/session/session.cpp
    src/session/receiver.cpp
)
add_library(tapein::core ALIAS tapein_core)

target_include_directories(tapein_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(tapein_core PUBLIC cxx_std_20)
target_link_libraries(tapein_core
    PRIVATE tapein::warnings
    PRIVATE MbedTLS::mbedcrypto
    PRIVATE alac::decoder)
if(WIN32)
    target_link_libraries(tapein_core PUBLIC ws2_32)
endif()
