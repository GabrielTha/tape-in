// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "MainComponent.h"

namespace tapein::app {

namespace {
core::session::BufferMode modeForIndex(int i)
{
    switch (i) {
    case 1:
        return core::session::BufferMode::shortBuffer;
    case 2:
        return core::session::BufferMode::monitoring;
    default:
        return core::session::BufferMode::senderSync;
    }
}
int indexForMode(core::session::BufferMode m)
{
    switch (m) {
    case core::session::BufferMode::shortBuffer:
        return 1;
    case core::session::BufferMode::monitoring:
        return 2;
    default:
        return 0;
    }
}
} // namespace

MainComponent::MainComponent(AppEngine& app, Settings& settings) : m_app(app), m_settings(settings)
{
    setSize(460, 360);

    m_title.setText("Tape In", juce::dontSendNotification);
    m_title.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    m_title.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(m_title);

    m_name.setText(m_settings.receiverName(), juce::dontSendNotification);
    m_name.setTextToShowWhenEmpty("Tape In", juce::Colour(0xff6b7177));
    m_name.setTooltip("The name this Mac shows under AirPlay on your phone.");
    m_name.onReturnKey = [this] { applyName(); };
    m_name.onFocusLost = [this] { applyName(); };
    addAndMakeVisible(m_name);

    m_output.setTextWhenNothingSelected("Output device");
    m_output.onChange = [this] {
        if (m_output.getSelectedId() > 0) {
            m_app.audio().selectOutput(m_output.getText());
        }
    };
    addAndMakeVisible(m_output);

    m_rate.setFont(juce::FontOptions(12.0f));
    m_rate.setColour(juce::Label::textColourId, juce::Colour(0xff9aa0a6));
    m_rate.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(m_rate);

    m_login.onClick = [this] {
        const auto r = m_app.loginItem().setEnabled(m_login.getToggleState());
        if (!r.ok()) {
            juce::Logger::writeToLog("start at login: " + juce::String(r.error().message));
        }
        m_settings.setStartAtLogin(m_login.getToggleState());
        refreshLoginItem();
    };
    addAndMakeVisible(m_login);

    m_loginHint.setFont(juce::FontOptions(11.0f));
    m_loginHint.setColour(juce::Label::textColourId, juce::Colour(0xfff0c419));
    addChildComponent(m_loginHint);

    m_advancedToggle.setClickingTogglesState(true);
    m_advancedToggle.onClick = [this] {
        m_advancedOpen = m_advancedToggle.getToggleState();
        m_advanced.setVisible(m_advancedOpen);
        resized();
    };
    addAndMakeVisible(m_advancedToggle);

    m_bufferMode.addItem("Sender sync (\xe2\x89\x88 2 s, default)", 1);
    m_bufferMode.addItem("Short (500 ms)", 2);
    m_bufferMode.addItem("Monitoring (150 ms, best effort)", 3);
    m_bufferMode.setSelectedItemIndex(indexForMode(m_settings.bufferMode()), juce::dontSendNotification);
    m_bufferMode.onChange = [this] {
        const auto mode = modeForIndex(m_bufferMode.getSelectedItemIndex());
        m_app.engine()->setBufferMode(mode);
        m_settings.setBufferMode(mode);
    };
    m_advanced.addAndMakeVisible(m_bufferMode);

    m_ignoreVolume.setToggleState(m_settings.ignoreSenderVolume(), juce::dontSendNotification);
    m_ignoreVolume.onClick = [this] {
        m_app.engine()->setIgnoreSenderVolume(m_ignoreVolume.getToggleState());
        m_settings.setIgnoreSenderVolume(m_ignoreVolume.getToggleState());
    };
    m_advanced.addAndMakeVisible(m_ignoreVolume);
    addChildComponent(m_advanced);

    m_banner.setColour(juce::Label::textColourId, juce::Colour(0xffe4572e));
    m_banner.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    addChildComponent(m_banner);
    m_openSettings.onClick = [this] {
        m_app.loginItem().openSystemSettings();
        juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork").launchInDefaultBrowser();
    };
    addChildComponent(m_openSettings);

    m_empty.setJustificationType(juce::Justification::centred);
    m_empty.setColour(juce::Label::textColourId, juce::Colour(0xff9aa0a6));
    m_empty.setText("Waiting for senders. Pick \"" + m_app.engine()->name()
                        + "\" under AirPlay on your phone.\n\nWorks with AirPlay. Not affiliated with Apple Inc.",
                    juce::dontSendNotification);
    addChildComponent(m_empty);

    m_viewport.setViewedComponent(&m_rowHost, false);
    m_viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(m_viewport);

    m_login.setToggleState(m_settings.startAtLogin(), juce::dontSendNotification);
    refreshOutputs();
    refreshLoginItem();
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::applyName()
{
    const juce::String name = m_name.getText().trim();
    m_app.engine()->setName(name.toStdString());
    const juce::String applied = m_app.engine()->name();
    m_settings.setReceiverName(applied);
    m_name.setText(applied, juce::dontSendNotification);
    m_empty.setText("Waiting for senders. Pick \"" + applied
                        + "\" under AirPlay on your phone.\n\nWorks with AirPlay. Not affiliated with Apple Inc.",
                    juce::dontSendNotification);
}

void MainComponent::refreshOutputs()
{
    m_output.clear(juce::dontSendNotification);
    const auto names = m_app.audio().outputDeviceNames();
    for (int i = 0; i < names.size(); ++i) {
        m_output.addItem(names[i], i + 1);
    }
    const auto current = m_app.audio().currentOutputName();
    for (int i = 0; i < names.size(); ++i) {
        if (names[i] == current) {
            m_output.setSelectedId(i + 1, juce::dontSendNotification);
        }
    }
}

void MainComponent::refreshLoginItem()
{
    using Status = core::platform::ILoginItem::Status;
    const auto status = m_app.loginItem().status();
    m_login.setToggleState(status == Status::enabled, juce::dontSendNotification);
    const bool needsApproval = status == Status::requiresApproval;
    m_loginHint.setText(needsApproval ? "Approve Tape In in System Settings > General > Login Items." : "",
                        juce::dontSendNotification);
    m_loginHint.setVisible(needsApproval);
}

void MainComponent::rebuildRows(const std::vector<core::session::SourceSnapshot>& snaps)
{
    bool sameSet = snaps.size() == m_rows.size();
    if (sameSet) {
        for (size_t i = 0; i < snaps.size(); ++i) {
            if (m_rows[i]->sourceId() != snaps[i].sourceId) {
                sameSet = false;
                break;
            }
        }
    }
    if (!sameSet) {
        m_rows.clear();
        for (const auto& snap : snaps) {
            auto row = std::make_unique<SourceRow>();
            row->onGain = [this](uint64_t id, float dB) { m_app.engine()->setUserGainDb(id, dB); };
            row->onMute = [this](uint64_t id, bool m) { m_app.engine()->setMute(id, m); };
            row->onSolo = [this](uint64_t id, bool s) { m_app.engine()->setSolo(id, s); };
            m_rowHost.addAndMakeVisible(*row);
            m_rows.push_back(std::move(row));
        }
        resized();
    }
    for (size_t i = 0; i < snaps.size() && i < m_rows.size(); ++i) {
        m_rows[i]->update(snaps[i]);
    }
    m_empty.setVisible(snaps.empty());
}

void MainComponent::timerCallback()
{
    (void)m_app.engine()->pollEvents();
    rebuildRows(m_app.engine()->sources());

    const double rate = m_app.audio().currentSampleRate();
    const char* mode = m_bufferMode.getSelectedItemIndex() == 1 ? "0.5 s"
                       : m_bufferMode.getSelectedItemIndex() == 2 ? "0.15 s" : "2 s";
    m_rate.setText(juce::String(rate / 1000.0, 1) + " kHz \xc2\xb7 buffer " + mode, juce::dontSendNotification);

    const bool denied = !m_app.lastError().isEmpty();
    m_banner.setVisible(denied);
    m_openSettings.setVisible(denied);
    if (denied) {
        m_banner.setText(m_app.lastError(), juce::dontSendNotification);
    }
    if (m_output.getNumItems() != m_app.audio().outputDeviceNames().size()) {
        refreshOutputs();
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced(14);
    auto header = r.removeFromTop(m_advancedOpen ? 176 : 132);

    auto titleRow = header.removeFromTop(28);
    m_title.setBounds(titleRow.removeFromLeft(120));
    m_rate.setBounds(titleRow);
    header.removeFromTop(6);

    m_name.setBounds(header.removeFromTop(28));
    header.removeFromTop(6);
    m_output.setBounds(header.removeFromTop(26));
    header.removeFromTop(6);

    auto loginRow = header.removeFromTop(24);
    m_login.setBounds(loginRow.removeFromLeft(140));
    m_advancedToggle.setBounds(loginRow.removeFromRight(100));
    m_loginHint.setBounds(header.removeFromTop(16));

    if (m_advancedOpen) {
        header.removeFromTop(4);
        m_advanced.setBounds(header);
        auto a = m_advanced.getLocalBounds();
        m_bufferMode.setBounds(a.removeFromTop(26));
        a.removeFromTop(4);
        m_ignoreVolume.setBounds(a.removeFromTop(24));
    }

    r.removeFromTop(8);
    if (m_banner.isVisible()) {
        m_banner.setBounds(r.removeFromTop(20));
        m_openSettings.setBounds(r.removeFromTop(24).removeFromLeft(180));
        r.removeFromTop(6);
    }

    m_empty.setBounds(r);
    m_viewport.setBounds(r);
    const int rowHeight = 64;
    m_rowHost.setSize(m_viewport.getWidth() - 8, std::max(r.getHeight(), static_cast<int>(m_rows.size()) * rowHeight));
    int y = 0;
    for (auto& row : m_rows) {
        row->setBounds(0, y, m_rowHost.getWidth(), rowHeight - 4);
        y += rowHeight;
    }
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff121417));
}

} // namespace tapein::app
