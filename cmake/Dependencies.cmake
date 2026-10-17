# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# Third-party dependencies fetched at configure time. Every entry here must
# also appear in THIRD_PARTY.md with its license.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Mbed TLS 3.6 LTS (Apache-2.0 OR GPL-2.0-or-later): RSA (raw PKCS#1 v1.5
# signing, OAEP-SHA1 decryption) and AES-128-CBC for the RAOP handshake.
# The release tarball is self-contained (the git tree needs a submodule).
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_CONFIG_FILE "${CMAKE_SOURCE_DIR}/third_party/mbedtls_config.h" CACHE FILEPATH "" FORCE)
FetchContent_Declare(mbedtls
    URL      https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6)
FetchContent_MakeAvailable(mbedtls)

if(TAPEIN_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

# libsamplerate 0.2.2 (BSD-2-Clause): sinc resampling with a variable ratio,
# used by the per-source drift control. process() does not allocate.
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
# 0.2.2 declares cmake_minimum_required(3.1..3.18); CMake 4 refuses < 3.5
# unless told which policy baseline to assume for the subproject.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(libsamplerate
    GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
    GIT_TAG        0.2.2
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(libsamplerate)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# JUCE 9.0.4 (AGPL-3.0, the option Tape In uses): the app layer only. The
# core and platform libraries never see it.
if(TAPEIN_BUILD_APP)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()
