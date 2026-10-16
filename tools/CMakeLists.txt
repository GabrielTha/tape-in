# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors

# Everything of tapein-cli except main() lives in a small library so the
# unit tests can exercise argument parsing and the WAV sink.
add_library(tapein_cli_lib STATIC
    tapein-cli/args.cpp
    tapein-cli/wav_sink.cpp
)
add_library(tapein::cli_lib ALIAS tapein_cli_lib)
target_include_directories(tapein_cli_lib PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(tapein_cli_lib PUBLIC tapein::core tapein::platform PRIVATE tapein::warnings)

if(TAPEIN_BUILD_TOOLS)
    add_executable(tapein-cli tapein-cli/main.cpp)
    target_link_libraries(tapein-cli PRIVATE tapein::cli_lib tapein::warnings)
endif()
