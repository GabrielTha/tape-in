# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# TAPEIN_SANITIZE="address,undefined" (or "thread") enables sanitizers for
# the whole build, including vendored code, so that the core's tests run
# under them on CI.

if(TAPEIN_SANITIZE STREQUAL "")
    return()
endif()

string(REPLACE "," ";" _tapein_sanitizers "${TAPEIN_SANITIZE}")

if(MSVC)
    if("address" IN_LIST _tapein_sanitizers)
        add_compile_options(/fsanitize=address)
        # ASan on MSVC is incompatible with incremental linking and /RTC.
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        add_link_options(/INCREMENTAL:NO)
    endif()
    foreach(_s IN LISTS _tapein_sanitizers)
        if(NOT _s STREQUAL "address")
            message(WARNING "Sanitizer '${_s}' is not supported by MSVC; ignored")
        endif()
    endforeach()
else()
    set(_flag "-fsanitize=${TAPEIN_SANITIZE}")
    add_compile_options(${_flag} -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(${_flag})
endif()

message(STATUS "Tape In: sanitizers enabled: ${TAPEIN_SANITIZE}")
