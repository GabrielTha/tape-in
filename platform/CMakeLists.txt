# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# tapein_platform: per-OS implementations of the core's platform interfaces.
# Objective-C++ (.mm) is allowed only under platform/macos/.

add_library(tapein_platform STATIC
    null/null_advertiser.cpp
)
add_library(tapein::platform ALIAS tapein_platform)

target_include_directories(tapein_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(tapein_platform PUBLIC tapein::core PRIVATE tapein::warnings)
