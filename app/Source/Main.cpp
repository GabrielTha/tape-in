// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Application entry point for the Tape In app.

#include <juce_gui_extra/juce_gui_extra.h>

namespace tapein::app {

class MainWindow final : public juce::DocumentWindow {
public:
    MainWindow()
        : juce::DocumentWindow("Tape In", juce::Colours::darkgrey, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        auto* label = new juce::Label({}, "Tape In \xe2\x80\x94 Phase 2 skeleton");
        label->setJustificationType(juce::Justification::centred);
        label->setSize(360, 240);
        setContentOwned(label, true);
        setResizable(true, false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }
};

class TapeInApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override { window = std::make_unique<MainWindow>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MainWindow> window;
};

} // namespace tapein::app

START_JUCE_APPLICATION(tapein::app::TapeInApplication)
