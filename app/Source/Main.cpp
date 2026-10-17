// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Application entry point for the Tape In app.

#include "AppEngine.h"
#include "AboutComponent.h"
#include "MainComponent.h"
#include "Settings.h"
#include "TrayIcon.h"

#include "tapein/core/session/engine.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace tapein::app {

class MainWindow final : public juce::DocumentWindow {
public:
    MainWindow(AppEngine& app, Settings& settings)
        : juce::DocumentWindow("Tape In", juce::Colour(0xff121417), juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(app, settings), true);
        setResizable(true, true);
        setResizeLimits(420, 320, 900, 1200);
        const auto saved = settings.windowBounds();
        if (!saved.isEmpty()) {
            setBounds(saved);
        } else {
            centreWithSize(getWidth(), getHeight());
        }
        m_settings = &settings;
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }
    void moved() override { save(); }
    void resized() override { juce::DocumentWindow::resized(); save(); }

private:
    void save()
    {
        if (m_settings != nullptr && isShowing()) {
            m_settings->setWindowBounds(getBounds());
        }
    }
    Settings* m_settings = nullptr;
};

class StatusLogger final : public juce::Timer {
public:
    explicit StatusLogger(core::session::Engine& engine) : m_engine(engine) { startTimer(2000); }
    void timerCallback() override
    {
        for (const auto& s : m_engine.sources()) {
            static const char* names[] = {"buffering", "playing", "fadingOut", "refilling", "stopped"};
            juce::Logger::writeToLog("source " + juce::String(s.sourceId) + " '" + s.clientName + "' "
                                     + names[static_cast<int>(s.state)] + " level " + juce::String(s.peakL, 2)
                                     + " packets " + juce::String(s.stats.packetsReceived)
                                     + " dropouts " + juce::String(s.underruns)
                                     + (s.live ? "" : " (disconnected)"));
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
        window = std::make_unique<MainWindow>(*app, *settings);
        tray = std::make_unique<TrayIcon>(app->loginItem());
        tray->onToggleWindow = [this] { toggleWindow(); };
        tray->onAbout = [] { AboutComponent::show(); };
        tray->onQuit = [this] { systemRequestedQuit(); };
        juce::Logger::writeToLog("Tape In ready on port "
                                 + juce::String(app->engine() != nullptr ? app->engine()->rtspPort() : 0));
        if (app->engine() != nullptr) {
            status = std::make_unique<StatusLogger>(*app->engine());
        }
    }

    void toggleWindow()
    {
        if (window == nullptr) {
            return;
        }
        const bool show = !window->isVisible();
        window->setVisible(show);
        if (show) {
            window->toFront(true);
        }
    }
    void shutdown() override
    {
        status.reset();
        tray.reset();
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
    std::unique_ptr<TrayIcon> tray;
};

} // namespace tapein::app

START_JUCE_APPLICATION(tapein::app::TapeInApplication)
