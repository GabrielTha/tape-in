# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# Defines the INTERFACE target tapein::warnings. Link it PRIVATE into every
# first-party target. Vendored third-party targets do not link it.

add_library(tapein_warnings INTERFACE)
add_library(tapein::warnings ALIAS tapein_warnings)

if(MSVC)
    target_compile_options(tapein_warnings INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    target_compile_definitions(tapein_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(tapein_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
        -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough
        -Werror=return-type)
endif()
