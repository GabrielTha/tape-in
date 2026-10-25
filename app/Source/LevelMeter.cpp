// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "LevelMeter.h"

#include <cmath>

namespace tapein::app {

namespace {
constexpr float kFloorDb = -60.0f;
constexpr float kWarnDb = -12.0f; ///< green below, amber above
constexpr float kClipDb = -3.0f; ///< amber below, red above
constexpr int kHoldTicks = 42; ///< ~1.4 s at 30 Hz before the hold releases
constexpr int kClipTicks = 45; ///< ~1.5 s clip light, as the reference meter

const juce::Colour kTrough{0xff26292e};
const juce::Colour kOk{0xff3fb950};
const juce::Colour kWarn{0xfff0c419};
const juce::Colour kClip{0xffe4572e};

// Linear 0..1 -> position 0..1 on the −60..0 dBFS scale.
float toNorm(float linear)
{
    if (linear <= 1.0e-5f) {
        return 0.0f;
    }
    const float db = juce::jlimit(kFloorDb, 0.0f, 20.0f * std::log10(linear));
    return (db - kFloorDb) / -kFloorDb;
}

juce::Colour zoneColour(float norm)
{
    constexpr float warnNorm = (kWarnDb - kFloorDb) / -kFloorDb;
    constexpr float clipNorm = (kClipDb - kFloorDb) / -kFloorDb;
    return norm > clipNorm ? kClip : norm > warnNorm ? kWarn : kOk;
}

// Fills `area` up to `norm`, split into the green/amber/red zone segments so a
// hot signal shows all three instead of recolouring the whole bar.
void fillZones(juce::Graphics& g, juce::Rectangle<float> area, float norm, float alpha)
{
    constexpr float warnNorm = (kWarnDb - kFloorDb) / -kFloorDb;
    constexpr float clipNorm = (kClipDb - kFloorDb) / -kFloorDb;
    const float w = area.getWidth();
    struct Zone {
        float from, to;
        juce::Colour colour;
    };
    const Zone zones[] = {{0.0f, warnNorm, kOk}, {warnNorm, clipNorm, kWarn}, {clipNorm, 1.0f, kClip}};
    for (const auto& z : zones) {
        const float to = std::min(norm, z.to);
        if (to <= z.from) {
            break;
        }
        g.setColour(z.colour.withAlpha(alpha));
        g.fillRoundedRectangle(area.getX() + z.from * w, area.getY(), (to - z.from) * w, area.getHeight(), 1.5f);
    }
}
} // namespace

void LevelMeter::feed(Channel& ch, float peak, float rms)
{
    // Peak: instant attack, steady release.
    ch.peak = peak >= ch.peak ? peak : ch.peak * 0.85f + peak * 0.15f;
    // RMS: averaged both ways; a negative input derives it from the peak.
    const float target = rms >= 0.0f ? rms : peak * 0.7f;
    ch.rms += (target - ch.rms) * (target > ch.rms ? 0.35f : 0.12f);
    // Peak hold: ride the maximum, then release after the hold time.
    if (peak >= ch.hold) {
        ch.hold = peak;
        ch.holdAge = 0;
    } else if (++ch.holdAge > kHoldTicks) {
        ch.hold *= 0.8f;
    }
}

void LevelMeter::setLevels(float peakL, float peakR, float rms)
{
    if (m_dimmed) {
        return; // a muted strip's meter stays cleared
    }
    feed(m_left, peakL, rms);
    feed(m_right, peakR, rms);
    if (std::max(peakL, peakR) >= 0.999f) {
        m_clipAge = 0;
    } else if (m_clipAge < 1000) {
        ++m_clipAge;
    }
    repaint();
}

void LevelMeter::setDimmed(bool dimmed)
{
    if (dimmed == m_dimmed) {
        return;
    }
    m_dimmed = dimmed;
    if (dimmed) {
        // The reference mixer clears a muted channel's meter immediately.
        m_left = {};
        m_right = {};
        m_clipAge = 1000;
    }
    repaint();
}

void LevelMeter::drawTrack(juce::Graphics& g, juce::Rectangle<float> area, const Channel& ch) const
{
    const float alpha = m_dimmed ? 0.35f : 1.0f;
    g.setColour(kTrough.withAlpha(alpha));
    g.fillRoundedRectangle(area, 2.0f);
    if (m_dimmed) {
        return;
    }
    // Translucent peak under a solid averaged bar, zone-coloured.
    fillZones(g, area, toNorm(ch.peak), 0.35f);
    fillZones(g, area, toNorm(ch.rms), 1.0f);
    // Peak-hold tick.
    const float holdNorm = toNorm(ch.hold);
    if (holdNorm > 0.01f) {
        g.setColour(zoneColour(holdNorm).withAlpha(0.95f));
        const float x = area.getX() + holdNorm * area.getWidth();
        g.fillRect(std::min(x, area.getRight() - 2.0f), area.getY(), 2.0f, area.getHeight());
    }
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced(1.0f);

    // Clip light at the right end, lit for a moment after a full-scale peak.
    const float led = 5.0f;
    auto ledArea = area.removeFromRight(led + 4.0f);
    const bool lit = !m_dimmed && m_clipAge < kClipTicks;
    g.setColour(lit ? kClip : kTrough.withAlpha(m_dimmed ? 0.35f : 1.0f));
    g.fillEllipse(juce::Rectangle<float>(led, led).withCentre({ledArea.getRight() - led * 0.5f - 1.0f,
                                                              ledArea.getCentreY()}));

    const float gap = 2.0f;
    const float h = (area.getHeight() - gap) * 0.5f;
    drawTrack(g, area.removeFromTop(h), m_left);
    area.removeFromTop(gap);
    drawTrack(g, area.removeFromTop(h), m_right);
}

} // namespace tapein::app
