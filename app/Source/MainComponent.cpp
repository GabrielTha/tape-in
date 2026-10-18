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
const char* latencyFor(int index)
{
    return index == 1 ? "~0.5" : index == 2 ? "~0.15" : "~2.0";
}
constexpr int kRowHeight = 130;
} // namespace

// The settings gear: a raised square that toggles the modal.
class MainComponent::GearButton final : public juce::Button {
public:
    GearButton() : juce::Button({}) { setClickingTogglesState(true); }
    void paintButton(juce::Graphics& g, bool highlighted, bool /*down*/) override
    {
        auto b = getLocalBounds().toFloat();
        if (getToggleState()) {
            theme::drawInset(g, b, 10.0f, theme::colours::slot);
        } else {
            theme::drawRaised(g, b.reduced(1.0f), 9.0f, theme::colours::background);
        }
        const juce::Colour ink = getToggleState()   ? theme::colours::accent
                                 : highlighted       ? juce::Colours::white
                                                     : juce::Colour(0xffb9bfc7);
        theme::icons::gear(g, juce::Rectangle<float>(17, 17).withCentre(b.getCentre()), ink);
    }
};

MainComponent::MainComponent(AppEngine& app, Settings& settings) : m_app(app), m_settings(settings)
{
    setLookAndFeel(&m_lnf);

    m_device.onSelect = [this](const juce::String& name) { m_app.audio().selectOutput(name); };
    addAndMakeVisible(m_device);

    m_gear = std::make_unique<GearButton>();
    m_gear->onClick = [this] { openSettings(m_gear->getToggleState()); };
    addAndMakeVisible(*m_gear);

    m_modal.onApply = [this](const SettingsModal::Values& v) { applySettings(v); };
    m_modal.onClose = [this] {
        m_gear->setToggleState(false, juce::dontSendNotification);
        openSettings(false);
    };
    addChildComponent(m_modal);

    // Local Network permission banner.
    m_banner.setColour(juce::Label::textColourId, juce::Colour(0xffe4572e));
    m_banner.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    addChildComponent(m_banner);
    m_openSettings.onClick = [this] {
        m_app.loginItem().openSystemSettings();
        juce::URL("x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork")
            .launchInDefaultBrowser();
    };
    addChildComponent(m_openSettings);

    // Source list.
    m_viewport.setViewedComponent(&m_rowHost, false);
    m_viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(m_viewport);

    // Buffer-mode footer (a segmented control painted over an inset trough).
    for (int i = 0; i < 3; ++i) {
        auto& b = m_buffer[static_cast<size_t>(i)];
        b.setClickingTogglesState(true);
        b.setRadioGroupId(42);
        b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8b929c));
        b.setColour(juce::TextButton::textColourOnId, theme::colours::text);
        b.onClick = [this, i] { selectBuffer(i); };
        addAndMakeVisible(b);
    }
    m_bufferIndex = indexForMode(m_settings.bufferMode());
    m_buffer[static_cast<size_t>(m_bufferIndex)].setToggleState(true, juce::dontSendNotification);

    m_status.setFont(juce::FontOptions(11.0f));
    m_status.setColour(juce::Label::textColourId, juce::Colour(0xff5f6872));
    m_status.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(m_status);

    refreshOutputs();
    setSize(500, 726); // after every child exists: setSize drives resized()
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void MainComponent::selectBuffer(int index)
{
    m_bufferIndex = index;
    const auto mode = modeForIndex(index);
    m_app.engine()->setBufferMode(mode);
    m_settings.setBufferMode(mode);
    repaint();
}

void MainComponent::openSettings(bool open)
{
    if (open) {
        SettingsModal::Values v;
        v.name = m_app.engine()->name();
        v.requirePassword = m_settings.requirePassword();
        v.password = m_settings.password();
        v.exclusiveSolo = m_settings.exclusiveSolo();
        v.audioDevice = m_app.audio().currentOutputName();
        m_modal.setAudioDevices(m_app.audio().outputDeviceNames());
        m_modal.setValues(v);
    }
    m_modal.setVisible(open);
    if (open) {
        m_modal.toFront(true);
    }
}

void MainComponent::applySettings(const SettingsModal::Values& v)
{
    m_app.engine()->setName(v.name.toStdString());
    const juce::String applied = m_app.engine()->name();
    m_settings.setReceiverName(applied);

    m_app.engine()->setRequirePassword(v.requirePassword, v.password.toStdString());
    m_settings.setRequirePassword(v.requirePassword);
    m_settings.setPassword(v.password);

    m_app.engine()->setExclusiveSolo(v.exclusiveSolo);
    m_settings.setExclusiveSolo(v.exclusiveSolo);

    if (v.audioDevice.isNotEmpty() && v.audioDevice != m_app.audio().currentOutputName()) {
        m_app.audio().selectOutput(v.audioDevice);
    }
}

void MainComponent::refreshOutputs()
{
    m_device.setDevices(m_app.audio().outputDeviceNames(), m_app.audio().currentOutputName());
}

void MainComponent::rebuildRows(const std::vector<core::session::SourceSnapshot>& snaps)
{
    const int desired = std::max(3, static_cast<int>(snaps.size()));
    if (static_cast<int>(m_rows.size()) != desired) {
        m_rows.clear();
        for (int i = 0; i < desired; ++i) {
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
    for (int i = 0; i < desired; ++i) {
        if (i < static_cast<int>(snaps.size())) {
            m_rows[static_cast<size_t>(i)]->update(snaps[static_cast<size_t>(i)]);
        } else {
            m_rows[static_cast<size_t>(i)]->setEmpty();
        }
    }
    m_liveCount = static_cast<int>(snaps.size());
}

void MainComponent::timerCallback()
{
    (void)m_app.engine()->pollEvents();
    rebuildRows(m_app.engine()->sources());

    if (m_liveCount == 0) {
        m_status.setText("waiting for a device\xe2\x80\xa6", juce::dontSendNotification);
    } else {
        m_status.setText(juce::String(m_liveCount) + (m_liveCount == 1 ? " source \xc2\xb7 " : " sources \xc2\xb7 ")
                             + latencyFor(m_bufferIndex) + " s latency",
                         juce::dontSendNotification);
    }

    const bool denied = !m_app.lastError().isEmpty();
    m_banner.setVisible(denied);
    m_openSettings.setVisible(denied);
    if (denied) {
        m_banner.setText(m_app.lastError(), juce::dontSendNotification);
    }
    if (m_device.isVisible() && !m_gear->getToggleState()) {
        refreshOutputs();
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    // Top bar.
    auto bar = r.removeFromTop(64).reduced(18, 0);
    m_gear->setBounds(bar.removeFromRight(36).withSizeKeepingCentre(36, 36));
    m_device.setBounds(juce::Rectangle<int>(0, 0, 210, 36).withCentre({getWidth() / 2, 32}));

    // Banner.
    if (m_banner.isVisible()) {
        auto b = r.removeFromTop(48).reduced(18, 0);
        m_banner.setBounds(b.removeFromTop(20));
        m_openSettings.setBounds(b.removeFromTop(24).removeFromLeft(180));
    }

    // Footer.
    auto footer = r.removeFromBottom(60).reduced(18, 14);
    footer.removeFromLeft(52); // "BUFFER" label (painted)
    auto trough = footer.removeFromLeft(210);
    auto seg = trough.reduced(4);
    const int sw = seg.getWidth() / 3;
    for (int i = 0; i < 3; ++i) {
        m_buffer[static_cast<size_t>(i)].setBounds(seg.removeFromLeft(i == 2 ? seg.getWidth() : sw).reduced(1));
    }
    m_status.setBounds(footer.withTrimmedLeft(10));

    // Source list.
    auto list = r.reduced(18, 0).withTrimmedTop(2);
    m_viewport.setBounds(list);
    m_rowHost.setSize(m_viewport.getWidth() - 8,
                      std::max(list.getHeight(), static_cast<int>(m_rows.size()) * kRowHeight));
    int y = 0;
    for (auto& row : m_rows) {
        row->setBounds(0, y, m_rowHost.getWidth(), kRowHeight - 10);
        y += kRowHeight;
    }

    m_modal.setBounds(getLocalBounds());
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::background);

    // Brand: the "TAPE IN" cassette wordmark logo.
    const float cy = 32.0f;
    if (const auto& brand = theme::logo(); brand.isValid()) {
        const float logoH = 30.0f;
        const float logoW = logoH * static_cast<float>(brand.getWidth()) / static_cast<float>(brand.getHeight());
        g.setColour(juce::Colours::white); // full opacity for the image draw
        g.drawImage(brand, juce::Rectangle<float>(18.0f, cy - logoH / 2.0f, logoW, logoH),
                    juce::RectanglePlacement::centred);
    }

    // Separator under the top bar.
    g.setColour(juce::Colour(0x0affffff));
    g.fillRect(0, 63, getWidth(), 1);

    // Footer: "BUFFER" label + segmented trough + active pill.
    g.setColour(juce::Colour(0xff5f6872));
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    const auto troughRect =
        m_buffer[0].getBounds().getUnion(m_buffer[2].getBounds()).toFloat().expanded(4.0f, 4.0f);
    g.drawText("BUFFER", juce::Rectangle<int>(18, troughRect.getY(), 50, static_cast<int>(troughRect.getHeight())),
               juce::Justification::centredLeft);
    theme::drawInset(g, troughRect, 11.0f, theme::colours::inset);
    if (m_bufferIndex >= 0 && m_bufferIndex < 3) {
        theme::drawRaised(g, m_buffer[static_cast<size_t>(m_bufferIndex)].getBounds().toFloat().reduced(1.0f), 8.0f,
                          theme::colours::slot);
    }
}

} // namespace tapein::app
