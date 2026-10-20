// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "RecordBar.h"

#include "Theme.h"

#include <cmath>

namespace tapein::app {

namespace {
const juce::Colour kRecordRed{0xffe5484d};

juce::Font readoutFont()
{
    return juce::Font(juce::FontOptions(11.0f, juce::Font::bold));
}
} // namespace

// The round record button: a raised neumorphic disc with a red record dot that
// morphs into a stop square while recording.
class RecordBar::RecButton final : public juce::Button {
public:
    RecButton() : juce::Button({}) {}

    void setRecording(bool r)
    {
        m_recording = r;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        const float radius = b.getWidth() * 0.5f;
        if (down) {
            theme::drawInset(g, b.reduced(1.0f), radius, theme::colours::slot);
        } else {
            theme::drawRaised(g, b.reduced(2.0f), radius - 2.0f, theme::colours::slot);
        }
        juce::Colour ink = kRecordRed;
        if (highlighted) {
            ink = ink.brighter(0.15f);
        }
        g.setColour(ink);
        auto c = b.getCentre();
        if (m_recording) {
            // Stop: rounded square.
            const float s = b.getWidth() * 0.30f;
            g.fillRoundedRectangle(juce::Rectangle<float>(s, s).withCentre(c), 2.5f);
        } else {
            // Record: filled dot.
            const float d = b.getWidth() * 0.40f;
            g.fillEllipse(juce::Rectangle<float>(d, d).withCentre(c));
        }
    }

private:
    bool m_recording = false;
};

RecordBar::RecordBar()
{
    m_rec = std::make_unique<RecButton>();
    m_rec->onClick = [this] {
        if (onToggle) {
            onToggle(!m_recording);
        }
    };
    addAndMakeVisible(*m_rec);
    setInterceptsMouseClicks(true, true);
}

RecordBar::~RecordBar() = default;

void RecordBar::setRecording(bool recording, int elapsedSeconds)
{
    if (recording != m_recording) {
        m_recording = recording;
        m_rec->setRecording(recording);
    }
    if (elapsedSeconds != m_elapsed) {
        m_elapsed = elapsedSeconds;
    }
    repaint();
}

void RecordBar::setWaveform(std::vector<float> envelope)
{
    m_wave = std::move(envelope);
    repaint();
}

void RecordBar::setSample(const juce::File& file)
{
    if (m_sample == file) {
        return;
    }
    m_sample = file;
    setMouseCursor(m_sample.existsAsFile() ? juce::MouseCursor::DraggingHandCursor
                                           : juce::MouseCursor::NormalCursor);
    repaint();
}

juce::String RecordBar::readoutText() const
{
    if (m_recording) {
        return juce::String(m_elapsed / 60) + ":" + juce::String(m_elapsed % 60).paddedLeft('0', 2);
    }
    return m_sample.existsAsFile() ? "DRAG OUT" : "REC";
}

int RecordBar::readoutWidth() const
{
    // Sized to the current text so the waveform trough runs right up to it.
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(readoutFont(), readoutText()))) + 2;
}

juce::Rectangle<int> RecordBar::waveBounds() const
{
    auto r = getLocalBounds().reduced(18, 14);
    r.removeFromLeft(44 + 14); // record disc + gap
    r.removeFromRight(readoutWidth() + 12); // readout + gap
    return r;
}

void RecordBar::mouseDrag(const juce::MouseEvent& e)
{
    // Dragging the waveform exports the last written sample to another app
    // (Finder, Ableton…). The record button consumes its own drags.
    if (m_sample.existsAsFile() && waveBounds().contains(e.getMouseDownPosition()) && e.getDistanceFromDragStart() > 8) {
        juce::DragAndDropContainer::performExternalDragDropOfFiles({m_sample.getFullPathName()}, false, this);
    }
}

void RecordBar::resized()
{
    auto r = getLocalBounds().reduced(18, 14);
    m_rec->setBounds(r.removeFromLeft(44).withSizeKeepingCentre(40, 40));
}

void RecordBar::paint(juce::Graphics& g)
{
    // The waveform trough (inset well).
    const auto wave = waveBounds().toFloat();
    theme::drawInset(g, wave, 11.0f, theme::colours::inset);

    const bool ready = m_sample.existsAsFile();
    const juce::Colour wire = m_recording ? kRecordRed
                              : ready      ? theme::colours::accent
                                           : theme::colours::dim;

    // Centre-line envelope: one vertical bar per bin, mirrored about the mid.
    if (!m_wave.empty()) {
        auto inner = wave.reduced(8.0f, 6.0f);
        const float midY = inner.getCentreY();
        const float maxH = inner.getHeight() * 0.5f;
        const int n = static_cast<int>(m_wave.size());
        const float step = inner.getWidth() / static_cast<float>(n);
        const float bw = std::max(1.0f, step * 0.6f);
        g.setColour(wire.withAlpha(m_recording || ready ? 0.9f : 0.55f));
        for (int i = 0; i < n; ++i) {
            const float v = juce::jlimit(0.0f, 1.0f, m_wave[static_cast<size_t>(i)]);
            const float h = std::max(1.0f, v * maxH);
            const float x = inner.getX() + static_cast<float>(i) * step;
            g.fillRoundedRectangle(x, midY - h, bw, h * 2.0f, bw * 0.5f);
        }
    } else {
        g.setColour(theme::colours::dim.withAlpha(0.5f));
        g.fillRect(wave.getX() + 8.0f, wave.getCentreY() - 0.5f, wave.getWidth() - 16.0f, 1.0f);
    }

    // Right-hand readout: elapsed time while recording, otherwise a hint.
    // Its strip is sized to the text, so the trough ends right beside it.
    auto readout = getLocalBounds().reduced(18, 14).removeFromRight(readoutWidth());
    g.setFont(readoutFont());
    g.setColour(m_recording ? kRecordRed
                : ready     ? theme::colours::accent
                            : juce::Colour(0xff5f6872));
    g.drawText(readoutText(), readout, juce::Justification::centredRight);
}

} // namespace tapein::app
