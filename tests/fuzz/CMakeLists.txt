# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# Fuzz harnesses for every network-facing parser. Two modes:
#   TAPEIN_FUZZ=ON (clang with libFuzzer): real fuzzers, run by CI for a
#     bounded time each.
#   otherwise: the same harnesses with a regression main() that replays the
#     corpus; registered with CTest so every build (incl. ASan/UBSan) runs
#     every crash-reproducer and seed we have.

set(_harnesses rtsp_parser sdp rtp alac)
set(_corpus ${CMAKE_CURRENT_SOURCE_DIR}/corpus)

if(TAPEIN_FUZZ AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "TAPEIN_FUZZ needs clang (libFuzzer)")
endif()

foreach(_h IN LISTS _harnesses)
    add_executable(fuzz_${_h} ${_h}_fuzz.cpp)
    target_link_libraries(fuzz_${_h} PRIVATE tapein::core tapein::warnings)
    target_include_directories(fuzz_${_h} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(TAPEIN_FUZZ)
        target_compile_definitions(fuzz_${_h} PRIVATE TAPEIN_FUZZING=1)
        target_compile_options(fuzz_${_h} PRIVATE -fsanitize=fuzzer)
        target_link_options(fuzz_${_h} PRIVATE -fsanitize=fuzzer)
    else()
        add_test(NAME fuzz_regress_${_h} COMMAND fuzz_${_h} ${_corpus}/${_h})
        set_tests_properties(fuzz_regress_${_h} PROPERTIES LABELS fuzz)
    endif()
endforeach()
