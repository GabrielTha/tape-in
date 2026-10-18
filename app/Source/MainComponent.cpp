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
constexpr int kRowHeight = 132;
} // namespace

MainComponent::MainComponent(AppEngine& app, Settings& settings) : m_app(app), m_settings(settings)
{
    setLookAndFeel(&m_lnf);
    setSize(500, 620);

    m_output.setTextWhenNothingSelected("Output device");
    m_output.setJustificationType(juce::Justification::centred);
    m_output.onChange = [this] {
        if (m_output.getSelectedId() > 0)
            m_app.audio().selectOutput(m_output.getText());
    };
    addAndMakeVisible(m_output);

    m_gear.setButtonText(juce::String::fromUTF8("\xe2\x9a\x99")); // gear glyph
    m_gear.setClickingTogglesState(true);
    m_gear.setColour(juce::TextButton::buttonColourId, theme::colours::slot);
    m_gear.setColour(juce::TextButton::textColourOffId, theme::colours::muted);
    m_gear.setColour(juce::TextButton::textColourOnId, theme::colours::accent);
    m_gear.onClick = [this] {
        m_settingsOpen = m_gear.getToggleState();
        m_settingsPanel.setVisible(m_settingsOpen);
        resized();
    };
    addAndMakeVisible(m_gear);

    // --- settings panel ---
    m_nameLabel.setText("Network name", juce::dontSendNotification);
    m_nameLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    m_nameLabel.setColour(juce::Label::textColourId, theme::colours::faint);
    m_settingsPanel.addAndMakeVisible(m_nameLabel);

    m_name.setText(m_settings.receiverName(), juce::dontSendNotification);
    m_name.setTextToShowWhenEmpty("Tape In", theme::colours::faint);
    m_name.onReturnKey = [this] { applyName(); };
    m_name.onFocusLost = [this] { applyName(); };
    m_settingsPanel.addAndMakeVisible(m_name);

    m_login.onClick = [this] {
        const auto r = m_app.loginItem().setEnabled(m_login.getToggleState());
        if (!r.ok())
            juce::Logger::writeToLog("start at login: " + juce::String(r.error().message));
        m_settings.setStartAtLogin(m_login.getToggleState());
        refreshLoginItem();
    };
    m_settingsPanel.addAndMakeVisible(m_login);

    m_loginHint.setFont(juce::FontOptions(11.0f));
    m_loginHint.setColour(juce::Label::textColourId, theme::colours::accent);
    m_settingsPanel.addChildComponent(m_loginHint);

    m_ignoreVolume.setToggleState(m_settings.ignoreSenderVolume(), juce::dontSendNotification);
    m_ignoreVolume.onClick = [this] {
        m_app.engine()->setIgnoreSenderVolume(m_ignoreVolume.getToggleState());
        m_settings.setIgnoreSenderVolume(m_ignoreVolume.getToggleState());
    };
    m_settingsPanel.addAndMakeVisible(m_ignoreVolume);
    addChildComponent(m_settingsPanel);

    // --- Local Network banner ---
    m_banner.setColour(juce::Label::textColourId, juce::Colour(0xffe4572e));
    m_banner.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    addChildComponent(m_banner);
    m_openSettings.onClick = [this] {
        m_app.loginItem().openSystemSettings();
        juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork").launchInDefaultBrowser();
    };
    addChildComponent(m_openSettings);

    // --- source list ---
    m_empty.setJustificationType(juce::Justification::centred);
    m_empty.setColour(juce::Label::textColourId, theme::colours::muted);
    m_empty.setText("Waiting for a device.\nPick \"" + m_app.engine()->name()
                        + "\" under AirPlay on your phone.\n\nWorks with AirPlay. Not affiliated with Apple Inc.",
                    juce::dontSendNotification);
    addChildComponent(m_empty);

    m_viewport.setViewedComponent(&m_rowHost, false);
    m_viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(m_viewport);

    // --- buffer footer ---
    for (int i = 0; i < 3; ++i) {
        auto& b = m_buffer[static_cast<size_t>(i)];
        b.setClickingTogglesState(true);
        b.setRadioGroupId(42);
        b.setColour(juce::TextButton::buttonColourId, theme::colours::background);
        b.setColour(juce::TextButton::buttonOnColourId, theme::colours::slot);
        b.setColour(juce::TextButton::textColourOffId, theme::colours::muted);
        b.setColour(juce::TextButton::textColourOnId, theme::colours::text);
        b.onClick = [this, i] { selectBuffer(i); };
        addAndMakeVisible(b);
    }
    m_buffer[static_cast<size_t>(indexForMode(m_settings.bufferMode()))].setToggleState(true, juce::dontSendNotification);

    m_status.setFont(juce::FontOptions(11.0f));
    m_status.setColour(juce::Label::textColourId, theme::colours::faint);
    m_status.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(m_status);

    m_login.setToggleState(m_settings.startAtLogin(), juce::dontSendNotification);
    refreshOutputs();
    refreshLoginItem();
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void MainComponent::selectBuffer(int index)
{
    const auto mode = modeForIndex(index);
    m_app.engine()->setBufferMode(mode);
    m_settings.setBufferMode(mode);
}

void MainComponent::applyName()
{
    const juce::String name = m_name.getText().trim();
    m_app.engine()->setName(name.toStdString());
    const juce::String applied = m_app.engine()->name();
    m_settings.setReceiverName(applied);
    m_name.setText(applied, juce::dontSendNotification);
    m_empty.setText("Waiting for a device.\nPick \"" + applied
                        + "\" under AirPlay on your phone.\n\nWorks with AirPlay. Not affiliated with Apple Inc.",
                    juce::dontSendNotification);
}

void MainComponent::refreshOutputs()
{
    m_output.clear(juce::dontSendNotification);
    const auto names = m_app.audio().outputDeviceNames();
    for (int i = 0; i < names.size(); ++i)
        m_output.addItem(names[i], i + 1);
    const auto current = m_app.audio().currentOutputName();
    for (int i = 0; i < names.size(); ++i)
        if (names[i] == current)
            m_output.setSelectedId(i + 1, juce::dontSendNotification);
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
        for (size_t i = 0; i < snaps.size(); ++i) {
            auto row = std::make_unique<SourceRow>();
            row->onGain = [this](uint64_t id, float dB) { m_app.engine()->setUserGainDb(id, dB); };
            row->onMute = [this](uint64_t id, bool m) { m_app.engine()->setMute(id, m); };
            row->onSolo = [this](uint64_t id, bool s) { m_app.engine()->setSolo(id, s); };
            row->onTransport = [this](uint64_t id, core::session::TransportCommand cmd) {
                m_app.engine()->sendTransportCommand(id, cmd);
            };
            m_rowHost.addAndMakeVisible(*row);
            m_rows.push_back(std::move(row));
        }
        resized();
    }
    for (size_t i = 0; i < snaps.size() && i < m_rows.size(); ++i)
        m_rows[i]->update(snaps[i]);
    m_empty.setVisible(snaps.empty());
}

void MainComponent::timerCallback()
{
    (void)m_app.engine()->pollEvents();
    rebuildRows(m_app.engine()->sources());

    const double rate = m_app.audio().currentSampleRate();
    const int bi = indexForMode(m_settings.bufferMode());
    const char* buf = bi == 1 ? "0.5 s" : bi == 2 ? "0.15 s" : "2 s";
    m_status.setText(juce::String(rate / 1000.0, 1) + " kHz \xc2\xb7 " + juce::String(m_rows.size())
                         + (m_rows.size() == 1 ? " source \xc2\xb7 " : " sources \xc2\xb7 ") + buf,
                     juce::dontSendNotification);

    const bool denied = !m_app.lastError().isEmpty();
    m_banner.setVisible(denied);
    m_openSettings.setVisible(denied);
    if (denied)
        m_banner.setText(m_app.lastError(), juce::dontSendNotification);
    if (m_output.getNumItems() != m_app.audio().outputDeviceNames().size())
        refreshOutputs();
}

void MainComponent::layoutSettings(juce::Rectangle<int> area)
{
    m_settingsPanel.setBounds(area);
    auto a = m_settingsPanel.getLocalBounds().reduced(14, 12);
    m_nameLabel.setBounds(a.removeFromTop(16));
    m_name.setBounds(a.removeFromTop(34));
    a.removeFromTop(10);
    m_login.setBounds(a.removeFromTop(24));
    if (m_loginHint.isVisible())
        m_loginHint.setBounds(a.removeFromTop(16));
    m_ignoreVolume.setBounds(a.removeFromTop(24));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    // Top bar
    auto top = r.removeFromTop(64).reduced(18, 14);
    top.removeFromLeft(150); // brand (painted)
    m_gear.setBounds(top.removeFromRight(36));
    top.removeFromRight(12);
    m_output.setBounds(top.reduced(10, 0));

    // Settings panel (fold-down)
    if (m_settingsOpen) {
        const int h = m_loginHint.isVisible() ? 150 : 134;
        layoutSettings(r.removeFromTop(h).reduced(18, 0));
        r.removeFromTop(6);
    }

    // Banner
    if (m_banner.isVisible()) {
        auto b = r.removeFromTop(48).reduced(18, 0);
        m_banner.setBounds(b.removeFromTop(20));
        m_openSettings.setBounds(b.removeFromTop(24).removeFromLeft(180));
    }

    // Footer
    auto footer = r.removeFromBottom(58).reduced(18, 12);
    auto seg = footer.removeFromLeft(210);
    for (int i = 0; i < 3; ++i)
        m_buffer[static_cast<size_t>(i)].setBounds(seg.removeFromLeft(70).reduced(1));
    m_status.setBounds(footer);

    // Source list
    auto list = r.reduced(18, 0);
    m_empty.setBounds(list);
    m_viewport.setBounds(list);
    m_rowHost.setSize(m_viewport.getWidth() - 8,
                      std::max(list.getHeight(), static_cast<int>(m_rows.size()) * kRowHeight));
    int y = 0;
    for (auto& row : m_rows) {
        row->setBounds(0, y, m_rowHost.getWidth(), kRowHeight - 10);
        y += kRowHeight;
    }
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::background);

    // Brand: reel mark + Boldonse wordmark.
    const float cy = 32.0f;
    auto reel = juce::Rectangle<float>(18.0f, cy - 13.0f, 26.0f, 26.0f);
    theme::drawRaised(g, reel, 8.0f, theme::colours::background);
    g.setColour(theme::colours::accent);
    g.drawEllipse(reel.getCentreX() - 7, reel.getCentreY() - 7, 14, 14, 1.6f);
    g.fillEllipse(reel.getCentreX() - 2, reel.getCentreY() - 2, 4, 4);

    g.setColour(theme::colours::text);
    g.setFont(theme::boldonseFont(12.0f));
    g.drawText("TAPE IN", juce::Rectangle<float>(52.0f, cy - 9.0f, 110.0f, 18.0f), juce::Justification::centredLeft);

    // Separator under the top bar.
    g.setColour(juce::Colour(0x12ffffff));
    g.fillRect(0, 63, getWidth(), 1);
}

} // namespace tapein::app
