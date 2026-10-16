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
)
target_link_libraries(tapein_tests PRIVATE tapein::core tapein::warnings Catch2::Catch2WithMain alac::encoder)

include(Catch)
catch_discover_tests(tapein_tests)
