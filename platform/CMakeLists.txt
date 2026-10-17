# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# tapein_platform: per-OS implementations of the core's platform interfaces.
# Objective-C++ (.mm) is allowed only under platform/macos/.

set(_platform_sources null/null_advertiser.cpp null/null_login_item.cpp)
if(APPLE)
    list(APPEND _platform_sources macos/dns_sd_advertiser.cpp macos/mac_login_item.mm)
endif()

add_library(tapein_platform STATIC ${_platform_sources})
add_library(tapein::platform ALIAS tapein_platform)

target_include_directories(tapein_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(tapein_platform PUBLIC tapein::core PRIVATE tapein::warnings)
if(APPLE)
    target_compile_definitions(tapein_platform PUBLIC TAPEIN_HAS_DNS_SD=1 TAPEIN_HAS_LOGIN_ITEM=1)
    target_link_libraries(tapein_platform PRIVATE "-framework Foundation" "-framework ServiceManagement")
endif()
