// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "AppEngine.h"
#include "Settings.h"
#include "SourceRow.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace tapein::app {

/// The main window content: header (name, output, rate, start-at-login,
/// advanced), a scrolling list of source rows, and an empty-state notice.
class MainComponent final : public juce::Component, private juce::Timer {
public:
    MainComponent(AppEngine& app, Settings& settings);
    ~MainComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;
    void refreshOutputs();
    void refreshLoginItem();
    void rebuildRows(const std::vector<core::session::SourceSnapshot>& snaps);
    void applyName();

    AppEngine& m_app;
    Settings& m_settings;

    juce::Label m_title;
    juce::TextEditor m_name;
    juce::ComboBox m_output;
    juce::Label m_rate;
    juce::ToggleButton m_login{"Start at login"};
    juce::Label m_loginHint;

    juce::TextButton m_advancedToggle{"Advanced"};
    juce::Component m_advanced;
    juce::ComboBox m_bufferMode;
    juce::ToggleButton m_ignoreVolume{"Ignore sender volume"};
    bool m_advancedOpen = false;

    juce::Label m_banner;
    juce::TextButton m_openSettings{"Open System Settings"};
    juce::Label m_empty;

    juce::Viewport m_viewport;
    juce::Component m_rowHost;
    std::vector<std::unique_ptr<SourceRow>> m_rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace tapein::app
