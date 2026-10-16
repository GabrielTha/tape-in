# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# Apple Lossless Audio Codec reference implementation (Apache-2.0), vendored
# unchanged from macosforge/alac. See UPSTREAM and THIRD_PARTY.md.
#
# Three targets:
#   alac::common  - bit reader and endian helpers shared by both sides
#   alac::decoder - what Tape In ships; network-facing, so it keeps every
#                   sanitizer check except UBSan's shift-base (Apple's
#                   predictor code left-shifts negative values, which is
#                   defined two's-complement arithmetic on every target we
#                   build for)
#   alac::encoder - tests and tools only (round trips); Apple's encoder also
#                   performs misaligned 32-bit loads, so UBSan is disabled
#                   for it entirely. It is never linked into the product.

function(tapein_alac_relax_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W2 /wd4244 /wd4267)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
    set_target_properties(${target} PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
    target_include_directories(${target} SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

set(_ubsan_on FALSE)
if(NOT MSVC AND NOT TAPEIN_SANITIZE STREQUAL "" AND TAPEIN_SANITIZE MATCHES "undefined")
    set(_ubsan_on TRUE)
endif()

add_library(alac_common STATIC ALACBitUtilities.c EndianPortable.c)
add_library(alac::common ALIAS alac_common)
tapein_alac_relax_warnings(alac_common)

add_library(alac_decoder STATIC ALACDecoder.cpp ag_dec.c dp_dec.c matrix_dec.c)
add_library(alac::decoder ALIAS alac_decoder)
tapein_alac_relax_warnings(alac_decoder)
target_link_libraries(alac_decoder PUBLIC alac::common)
if(NOT MSVC)
    # Malformed packets make the predictor's int32 arithmetic overflow
    # (dp_dec.c:347, found by fuzzing). With -fwrapv that is defined
    # two's-complement wraparound: the output is garbage, but bounded, and
    # the compiler may not assume the overflow never happens.
    target_compile_options(alac_decoder PRIVATE -fwrapv)
endif()
if(_ubsan_on)
    target_compile_options(alac_decoder PRIVATE -fno-sanitize=shift-base)
endif()

add_library(alac_encoder STATIC ALACEncoder.cpp ag_enc.c dp_enc.c matrix_enc.c)
add_library(alac::encoder ALIAS alac_encoder)
tapein_alac_relax_warnings(alac_encoder)
target_link_libraries(alac_encoder PUBLIC alac::common)
if(_ubsan_on)
    target_compile_options(alac_encoder PRIVATE -fno-sanitize=undefined)
endif()
