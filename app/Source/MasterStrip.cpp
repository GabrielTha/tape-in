// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "MasterStrip.h"

#include "Theme.h"

#include <cmath>

namespace tapein::app {

namespace {
constexpr int kLabelWidth = 54;
constexpr int kReadoutWidth = 62;
constexpr int kMuteSize = 22;

juce::Font stripFont()
{
    return juce::Font(juce::FontOptions(10.0f, juce::Font::bold));
}
} // namespace

MasterStrip::MasterStrip()
{
    addAndMakeVisible(m_meter);

    m_mute.setClickingTogglesState(true);
    m_mute.setColour(juce::TextButton::buttonColourId, theme::colours::slot);
    m_mute.setColour(juce::TextButton::buttonOnColourId, theme::colours::accent);
    m_mute.setColour(juce::TextButton::textColourOffId, theme::colours::faint);
    m_mute.setColour(juce::TextButton::textColourOnId, theme::colours::background);
    m_mute.onClick = [this] {
        m_muted = m_mute.getToggleState();
        m_meter.setDimmed(m_muted);
        if (onMute) {
            onMute(m_muted);
        }
        repaint();
    };
    addAndMakeVisible(m_mute);
}

void MasterStrip::setLevels(float peakL, float peakR)
{
    m_meter.setLevels(peakL, peakR);
    const float peak = std::max(peakL, peakR);
    // The readout follows the meter's peak ballistics so the number is legible.
    m_peak = peak >= m_peak ? peak : m_peak * 0.85f + peak * 0.15f;
    repaint();
}

juce::String MasterStrip::readoutText() const
{
    if (m_muted) {
        return "MUTED";
    }
    if (m_peak <= 1.0e-5f) {
        return juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e dB"); // −∞ dB
    }
    const float db = juce::jlimit(-60.0f, 0.0f, 20.0f * std::log10(m_peak));
    return (db <= -60.0f ? juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e")
                         : juce::String(db, 1))
           + " dB";
}

void MasterStrip::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft(kLabelWidth); // painted label
    m_mute.setBounds(r.removeFromRight(kMuteSize).withSizeKeepingCentre(kMuteSize, kMuteSize));
    r.removeFromRight(8);
    r.removeFromRight(kReadoutWidth); // painted readout
    r.removeFromRight(8);
    m_meter.setBounds(r.reduced(0, 3));
}

void MasterStrip::paint(juce::Graphics& g)
{
    g.setFont(stripFont());
    g.setColour(m_muted ? theme::colours::dim : theme::colours::faint);
    g.drawText("MASTER", getLocalBounds().removeFromLeft(kLabelWidth).reduced(0, 1),
               juce::Justification::centredLeft);

    auto readout = getLocalBounds();
    readout.removeFromRight(kMuteSize + 8);
    readout = readout.removeFromRight(kReadoutWidth);
    g.setColour(m_muted ? theme::colours::dim : theme::colours::muted);
    g.drawText(readoutText(), readout.reduced(0, 1), juce::Justification::centredRight);
}

} // namespace tapein::app
