# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# The JUCE app "Tape In": status-bar icon, window, audio device, settings.
# Consumes tapein::platform and tapein::core; the only place JUCE is used.

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Info-merge.plist" _tapein_plist_merge)

juce_add_gui_app(TapeIn
    PRODUCT_NAME "Tape In"
    VERSION ${PROJECT_VERSION}
    BUNDLE_ID app.tapein.mac
    COMPANY_NAME "Tape In contributors"
    COMPANY_COPYRIGHT "Copyright (C) 2026 Tape In contributors, AGPL-3.0-or-later"
    PLIST_TO_MERGE "${_tapein_plist_merge}"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

juce_add_binary_data(TapeInData SOURCES Resources/fonts/Boldonse.ttf)

target_sources(TapeIn PRIVATE
    Source/Main.cpp
    Source/Settings.cpp
    Source/AudioOutput.cpp
    Source/AppEngine.cpp
    Source/Theme.cpp
    Source/LevelMeter.cpp
    Source/SourceRow.cpp
    Source/MainComponent.cpp
    Source/TrayIcon.cpp
    Source/AboutComponent.cpp
)

target_compile_definitions(TapeIn PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:TapeIn,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:TapeIn,JUCE_VERSION>"
    TAPEIN_SOURCE_URL="https://github.com/GabrielTha/tape-in"
)

target_link_libraries(TapeIn PRIVATE
    tapein::platform
    TapeInData
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
