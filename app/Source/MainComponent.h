// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "AppEngine.h"
#include "DeviceButton.h"
#include "DonateModal.h"
#include "MasterStrip.h"
#include "PixelCanvas.h"
#include "RecordBar.h"
#include "Settings.h"
#include "SettingsModal.h"
#include "SourceRow.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace tapein::app {

/// The vertical neumorphic main window (Main/Empty artboards): a top bar
/// (brand wordmark, centred output-device pill, settings gear), a list of
/// source slots padded to at least three, and a buffer-mode footer. The gear
/// opens the settings modal overlay.
class MainComponent final : public juce::Component, private juce::Timer {
public:
    MainComponent(AppEngine& app, Settings& settings);
    ~MainComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    class GearButton; ///< raised square painting the gear icon
    class DonateButton; ///< raised amber pill opening the donate overlay

    void timerCallback() override;
    void refreshOutputs();
    void rebuildRows(const std::vector<core::session::SourceSnapshot>& snaps);
    void openSettings(bool open);
    void applySettings(const SettingsModal::Values& v);
    void selectBuffer(int index);
    void toggleRecording(bool start);

    AppEngine& m_app;
    Settings& m_settings;
    theme::NeumorphicLookAndFeel m_lnf;

    // Decorative pixel grid behind everything.
    PixelCanvas m_pixels;

    // Top bar
    DeviceButton m_device;
    std::unique_ptr<GearButton> m_gear;

    // Settings modal overlay
    SettingsModal m_modal;

    // Local Network permission banner
    juce::Label m_banner;
    juce::TextButton m_openSettings{"Open System Settings"};

    // Source list
    juce::Viewport m_viewport;
    juce::Component m_rowHost;
    std::vector<std::unique_ptr<SourceRow>> m_rows;

    // Master channel strip, directly below the record bar.
    MasterStrip m_master;

    // Global sampling bar (above the footer).
    RecordBar m_record;
    juce::File m_lastSample; ///< the most recent sample written, for drag-out

    // Footer: colour-coded status (left) and the DONATE button (right).
    int m_bufferIndex = 0;
    juce::Label m_status;
    std::unique_ptr<DonateButton> m_donateBtn;
    DonateModal m_donate;
    int m_liveCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace tapein::app
