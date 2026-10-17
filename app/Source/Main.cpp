// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Application entry point for the Tape In app.

#include "AppEngine.h"
#include "Settings.h"

#include "tapein/core/session/engine.h"

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

class StatusLogger final : public juce::Timer {
public:
    explicit StatusLogger(core::session::Engine& engine) : m_engine(engine) { startTimerHz(1); }
    void timerCallback() override
    {
        for (const auto& s : m_engine.sources()) {
            static const char* names[] = {"buffering", "playing", "fadingOut", "refilling", "stopped"};
            juce::Logger::writeToLog("source " + juce::String(s.sourceId) + " '" + s.clientName + "' "
                                     + names[static_cast<int>(s.state)] + " fill " + juce::String(s.fillFrames)
                                     + " peak " + juce::String(s.peakL, 3) + (s.live ? "" : " (disconnected)"));
        }
    }

private:
    core::session::Engine& m_engine;
};

class TapeInApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        logFile = std::make_unique<juce::FileLogger>(
            juce::FileLogger::getSystemLogFileFolder().getChildFile("Tape In").getChildFile("Tape In.log"),
            "Tape In " + getApplicationVersion());
        juce::Logger::setCurrentLogger(logFile.get());
        settings = std::make_unique<Settings>();
        app = std::make_unique<AppEngine>(*settings);
        if (!app->start()) {
            juce::Logger::writeToLog("start failed: " + app->lastError());
        }
        window = std::make_unique<MainWindow>();
        juce::Logger::writeToLog("Tape In ready on port "
                                 + juce::String(app->engine() != nullptr ? app->engine()->rtspPort() : 0));
        if (app->engine() != nullptr) {
            status = std::make_unique<StatusLogger>(*app->engine());
        }
    }
    void shutdown() override
    {
        status.reset();
        window.reset();
        if (app != nullptr) {
            app->stop();
        }
        app.reset();
        settings.reset();
        juce::Logger::setCurrentLogger(nullptr);
        logFile.reset();
    }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<juce::FileLogger> logFile;
    std::unique_ptr<Settings> settings;
    std::unique_ptr<StatusLogger> status;
    std::unique_ptr<AppEngine> app;
    std::unique_ptr<MainWindow> window;
};

} // namespace tapein::app

START_JUCE_APPLICATION(tapein::app::TapeInApplication)
