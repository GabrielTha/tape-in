// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "MainComponent.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace tapein::app {

namespace {

// Writes a recorded sample to a 16-bit WAV in `folder`, named from the
// now-playing metadata plus a timestamp. Returns the file, or an invalid
// File on failure / empty capture.
juce::File writeSampleToWav(const core::session::RecordedSample& s, const juce::File& folder)
{
    if (s.interleaved.empty() || s.channels == 0) {
        return {};
    }
    folder.createDirectory();

    juce::String base = juce::String(s.artist).trim();
    const juce::String track = juce::String(s.trackName).trim();
    if (base.isNotEmpty() && track.isNotEmpty()) {
        base += " - ";
    }
    base += track;
    if (base.isEmpty()) {
        base = "Sample";
    }
    base = juce::File::createLegalFileName(base);
    const juce::String stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H.%M.%S");
    const juce::File out = folder.getNonexistentChildFile(base + " " + stamp, ".wav");

    auto stream = out.createOutputStream();
    if (stream == nullptr) {
        return {};
    }
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        fmt.createWriterFor(stream.get(), static_cast<double>(s.sampleRate), s.channels, 16, {}, 0));
    if (writer == nullptr) {
        return {};
    }
    stream.release(); // the writer owns the stream now

    const int numFrames = static_cast<int>(s.interleaved.size() / s.channels);
    juce::AudioBuffer<float> buf(static_cast<int>(s.channels), numFrames);
    for (int ch = 0; ch < static_cast<int>(s.channels); ++ch) {
        float* d = buf.getWritePointer(ch);
        for (int i = 0; i < numFrames; ++i) {
            d[i] = static_cast<float>(s.interleaved[static_cast<size_t>(i) * s.channels + static_cast<size_t>(ch)]) / 32768.0f;
        }
    }
    writer->writeFromAudioSampleBuffer(buf, 0, numFrames);
    writer.reset(); // flush + close
    return out;
}

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

// The footer's raised amber DONATE pill, the one loud control in the app.
class MainComponent::DonateButton final : public juce::Button {
public:
    DonateButton() : juce::Button({}) {}
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced(2.0f);
        auto fill = theme::colours::accent;
        if (highlighted) {
            fill = fill.brighter(0.08f);
        }
        if (down) {
            theme::drawInset(g, b, b.getHeight() * 0.5f, fill);
        } else {
            theme::drawRaised(g, b, b.getHeight() * 0.5f, fill);
        }
        g.setColour(juce::Colour(0xff1a1208));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("DONATE", b, juce::Justification::centred);
    }
};

MainComponent::MainComponent(AppEngine& app, Settings& settings) : m_app(app), m_settings(settings)
{
    setLookAndFeel(&m_lnf);

    addAndMakeVisible(m_pixels);
    addMouseListener(this, true); // child moves reach mouseMove() below

    m_device.onSelect = [this](const juce::String& name) {
        m_app.audio().selectOutput(name); // resets the routed pair to 1/2
        const auto [outL, outR] = m_app.audio().outputChannelPair();
        m_settings.setOutputChannels(outL, outR);
    };
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

    // Master output meter below the global sampling bar.
    addAndMakeVisible(m_master);

    // Global sampling bar (above the footer).
    m_record.onToggle = [this](bool start) { toggleRecording(start); };
    addAndMakeVisible(m_record);

    // Footer: the colour-coded status in the bottom-left corner and the
    // DONATE pill on the right. The buffer mode now lives in Settings.
    m_bufferIndex = indexForMode(m_settings.bufferMode());

    m_status.setFont(juce::FontOptions(11.0f));
    m_status.setColour(juce::Label::textColourId, juce::Colour(0xff5f6872));
    m_status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_status);

    m_donateBtn = std::make_unique<DonateButton>();
    m_donateBtn->onClick = [this] {
        m_donate.setVisible(true);
        m_donate.toFront(true);
    };
    addAndMakeVisible(*m_donateBtn);

    m_donate.onClose = [this] { m_donate.setVisible(false); };
    addChildComponent(m_donate);

    refreshOutputs();
    setSize(500, 792); // after every child exists: setSize drives resized()
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
        v.askBeforeAccept = m_settings.askBeforeAccept();
        v.samplesFolder = m_settings.samplesFolder().getFullPathName();
        v.audioDevice = m_app.audio().currentOutputName();
        v.bufferIndex = m_bufferIndex;
        std::tie(v.outLeft, v.outRight) = m_app.audio().outputChannelPair();
        m_modal.setAudioDevices(m_app.audio().outputDeviceNames());
        m_modal.setOutputChannelNames(m_app.audio().outputChannelNames());
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

    m_app.engine()->setAskBeforeAccept(v.askBeforeAccept);
    m_settings.setAskBeforeAccept(v.askBeforeAccept);

    if (v.samplesFolder.isNotEmpty()) {
        m_settings.setSamplesFolder(juce::File(v.samplesFolder));
    }

    if (v.audioDevice.isNotEmpty() && v.audioDevice != m_app.audio().currentOutputName()) {
        m_app.audio().selectOutput(v.audioDevice);
    }
    if (std::pair(v.outLeft, v.outRight) != m_app.audio().outputChannelPair()) {
        m_app.audio().selectOutputChannels(v.outLeft, v.outRight);
    }
    // Persist what actually applied (the pair is clamped to the device).
    const auto [outL, outR] = m_app.audio().outputChannelPair();
    m_settings.setOutputChannels(outL, outR);

    selectBuffer(juce::jlimit(0, 2, v.bufferIndex));
}

void MainComponent::toggleRecording(bool start)
{
    if (start) {
        m_app.engine()->startRecording();
        return;
    }
    const auto sample = m_app.engine()->stopRecording();
    const juce::File file = writeSampleToWav(sample, m_settings.samplesFolder());
    if (file.existsAsFile()) {
        m_lastSample = file;
        m_record.setSample(file);
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
            row->onApprove = [this](uint64_t id) { m_app.engine()->approveConnection(id); };
            row->onDecline = [this](uint64_t id) { m_app.engine()->declineConnection(id); };
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

    const auto rec = m_app.engine()->recordStatus();
    m_record.setRecording(rec.recording, static_cast<int>(rec.frames / 44100));
    m_record.setWaveform(m_app.engine()->inputWaveform());

    // Master output level: the meter strip plus the pixel-canvas shimmer.
    const auto [peakL, peakR] = m_app.engine()->mixer().consumePeaks();
    m_master.setLevels(peakL, peakR);
    m_pixels.energize(juce::jmax(peakL, peakR));

    if (m_liveCount == 0) {
        // CharPointer_UTF8 so the literal's UTF-8 bytes decode to the glyph
        // (an implicit const char* was rendering the ellipsis as mojibake).
        m_status.setText(juce::String(juce::CharPointer_UTF8("Waiting for a device\xe2\x80\xa6")),
                         juce::dontSendNotification);
        m_status.setColour(juce::Label::textColourId, juce::Colour(0xff5f6872)); // idle: dim
    } else {
        const juce::String sep = juce::CharPointer_UTF8(" \xc2\xb7 ");
        const juce::String unit = m_liveCount == 1 ? " source" : " sources";
        m_status.setText(juce::String(m_liveCount) + unit + sep + latencyFor(m_bufferIndex) + " s latency",
                         juce::dontSendNotification);
        m_status.setColour(juce::Label::textColourId, juce::Colour(0xff79b98a)); // live: green
    }
    if (!m_app.lastError().isEmpty()) {
        m_status.setColour(juce::Label::textColourId, juce::Colour(0xffe4572e)); // error: red
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

void MainComponent::mouseMove(const juce::MouseEvent& e)
{
    m_pixels.pointerAt(e.getEventRelativeTo(&m_pixels).position);
}

void MainComponent::mouseDrag(const juce::MouseEvent& e)
{
    m_pixels.pointerAt(e.getEventRelativeTo(&m_pixels).position);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    m_pixels.setBounds(r);

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

    // Footer: status bottom-left, DONATE bottom-right.
    auto footer = r.removeFromBottom(60).reduced(18, 14);
    m_donateBtn->setBounds(footer.removeFromRight(104).withSizeKeepingCentre(104, 30));
    m_status.setBounds(footer.withTrimmedRight(10));

    // Global record bar above the footer (full width, horizontal), with the
    // master output meter strip right below it.
    m_master.setBounds(r.removeFromBottom(14).reduced(18, 2));
    m_record.setBounds(r.removeFromBottom(68));

    // Source list. Trim the top so the header-to-first-slot gap matches the
    // inter-slot gap: slots are kRowHeight apart with a (kRowHeight - rowH) +
    // 3px card inset = 16px visible gap; the first card has one 3px inset, so
    // trim 13 to reach the same 16px below the header.
    auto list = r.reduced(18, 0).withTrimmedTop(13);
    m_viewport.setBounds(list);
    m_rowHost.setSize(m_viewport.getWidth() - 8,
                      std::max(list.getHeight(), static_cast<int>(m_rows.size()) * kRowHeight));
    int y = 0;
    for (auto& row : m_rows) {
        row->setBounds(0, y, m_rowHost.getWidth(), kRowHeight - 10);
        y += kRowHeight;
    }

    m_modal.setBounds(getLocalBounds());
    m_donate.setBounds(getLocalBounds());
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

}

} // namespace tapein::app
