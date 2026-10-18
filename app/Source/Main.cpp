// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Application entry point for the Tape In app.

#include "AppEngine.h"
#include "AboutComponent.h"
#include "DockReopen.h"
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
        setResizeLimits(460, 480, 900, 1400);
        const auto saved = settings.windowBounds();
        if (boundsAreUsable(saved)) {
            setBounds(saved);
        } else {
            // Empty, too small, or mostly off-screen (e.g. saved on another
            // monitor): fall back to the content's natural size, centred.
            centreWithSize(getWidth(), getHeight());
        }
        m_settings = &settings;
        setVisible(true);
        toFront(true);
    }

    /// A saved rectangle is only restored if it still fits the resize limits
    /// and lands substantially on a connected display, so the window can never
    /// open off-screen or too small to use.
    static bool boundsAreUsable(juce::Rectangle<int> b)
    {
        if (b.isEmpty() || b.getWidth() < 460 || b.getHeight() < 480) {
            return false;
        }
        const auto usable = juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
        const auto overlap = b.getIntersection(usable);
        return overlap.getWidth() >= 200 && overlap.getHeight() >= 120;
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
        juce::Process::makeForegroundProcess();
        // A regular Dock app: clicking the Dock icon with the window closed
        // reopens it (JUCE does not forward this on its own).
        installDockReopenHandler([this] { showWindow(); });
        tray = std::make_unique<TrayIcon>(app->loginItem());
        tray->onToggleWindow = [this] { toggleWindow(); };
        tray->onAbout = [] { AboutComponent::show(); };
        tray->onQuit = [this] { systemRequestedQuit(); };
        tray->onStartAtLoginChanged = [this](bool enabled) { settings->setStartAtLogin(enabled); };
        tray->isIgnoreSenderVolume = [this] { return settings->ignoreSenderVolume(); };
        tray->onIgnoreSenderVolume = [this](bool ignore) {
            if (app->engine() != nullptr) {
                app->engine()->setIgnoreSenderVolume(ignore);
            }
            settings->setIgnoreSenderVolume(ignore);
        };
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
        if (window->isVisible()) {
            window->setVisible(false);
        } else {
            showWindow();
        }
    }

    void showWindow()
    {
        if (window == nullptr) {
            return;
        }
        juce::Process::makeForegroundProcess();
        window->setVisible(true);
        window->setMinimised(false);
        window->toFront(true);
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
