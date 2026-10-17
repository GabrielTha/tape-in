// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "LevelMeter.h"

#include <cmath>

namespace tapein::app {

float LevelMeter::decayed(float current, float& held) const
{
    // Attack instantly, release slowly (held *= 0.85 each repaint ~30 Hz).
    held = current >= held ? current : held * 0.85f + current * 0.15f;
    return held;
}

void LevelMeter::setLevels(float peakL, float peakR)
{
    decayed(peakL, m_heldL);
    decayed(peakR, m_heldR);
    repaint();
}

void LevelMeter::drawBar(juce::Graphics& g, juce::Rectangle<float> area, float level) const
{
    g.setColour(juce::Colour(0xff26292e));
    g.fillRoundedRectangle(area, 2.0f);
    // Map linear level to dBFS over a −60..0 range.
    const float db = level > 1.0e-5f ? juce::jlimit(-60.0f, 0.0f, 20.0f * std::log10(level)) : -60.0f;
    const float norm = (db + 60.0f) / 60.0f;
    if (norm <= 0.0f) {
        return;
    }
    auto filled = area.withWidth(area.getWidth() * norm);
    const juce::Colour c = db > -3.0f ? juce::Colour(0xffe4572e) : db > -12.0f ? juce::Colour(0xfff0c419)
                                                                               : juce::Colour(0xff3fb950);
    g.setColour(c);
    g.fillRoundedRectangle(filled, 2.0f);
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced(1.0f);
    const float gap = 2.0f;
    const float h = (area.getHeight() - gap) * 0.5f;
    drawBar(g, area.removeFromTop(h), m_heldL);
    area.removeFromTop(gap);
    drawBar(g, area.removeFromTop(h), m_heldR);
}

} // namespace tapein::app
