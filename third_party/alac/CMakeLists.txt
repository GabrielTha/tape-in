# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# Apple Lossless Audio Codec reference implementation (Apache-2.0), vendored
# unchanged from macosforge/alac. See UPSTREAM and THIRD_PARTY.md. The
# encoder is only used by tests (round-trip) and tools.

add_library(alac_codec STATIC
    ag_dec.c
    ag_enc.c
    ALACBitUtilities.c
    ALACDecoder.cpp
    ALACEncoder.cpp
    dp_dec.c
    dp_enc.c
    EndianPortable.c
    matrix_dec.c
    matrix_enc.c
)
add_library(alac::codec ALIAS alac_codec)

target_include_directories(alac_codec SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(alac_codec PROPERTIES C_STANDARD 11 CXX_STANDARD 17)

# Third-party code is compiled with its own (relaxed) warning set.
if(MSVC)
    target_compile_options(alac_codec PRIVATE /W2 /wd4244 /wd4267)
    target_compile_definitions(alac_codec PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(alac_codec PRIVATE -w)
endif()
