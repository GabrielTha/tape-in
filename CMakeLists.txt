# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors

cmake_minimum_required(VERSION 3.25)

project(TapeIn
    VERSION 0.1.0
    DESCRIPTION "Open-source audio receiver that works with AirPlay"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(TAPEIN_BUILD_TESTS "Build unit tests" ON)
option(TAPEIN_BUILD_TOOLS "Build command-line tools" ON)
option(TAPEIN_FUZZ "Build libFuzzer harnesses (clang only)" OFF)
set(TAPEIN_SANITIZE "" CACHE STRING "Comma-separated sanitizers, e.g. address,undefined")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(Dependencies)

add_subdirectory(third_party/alac)
add_subdirectory(core)

if(TAPEIN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
