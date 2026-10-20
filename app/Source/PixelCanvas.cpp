// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "PixelCanvas.h"

#include "Theme.h"

#include <cmath>

namespace tapein::app {

namespace {
constexpr int kGap = 9;           ///< cell pitch in px
constexpr float kDecay = 0.035f;  ///< intensity lost per frame (30 Hz)
constexpr float kRadius = 2.4f;   ///< splat radius around the pointer, in cells
constexpr float kMaxAlpha = 0.4f; ///< alpha of a fully lit cell
} // namespace

PixelCanvas::PixelCanvas()
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

void PixelCanvas::resized()
{
    m_cols = juce::jmax(1, getWidth() / kGap);
    m_rows = juce::jmax(1, getHeight() / kGap);
    m_cells.assign(static_cast<size_t>(m_cols) * static_cast<size_t>(m_rows), 0.0f);
}

void PixelCanvas::pointerAt(juce::Point<float> pos)
{
    if (m_cells.empty()) {
        return;
    }
    const float cx = pos.x / static_cast<float>(kGap);
    const float cy = pos.y / static_cast<float>(kGap);
    const int r = static_cast<int>(std::ceil(kRadius));
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const int col = static_cast<int>(cx) + dx;
            const int row = static_cast<int>(cy) + dy;
            if (col < 0 || col >= m_cols || row < 0 || row >= m_rows) {
                continue;
            }
            const float dist = std::hypot(static_cast<float>(col) + 0.5f - cx, static_cast<float>(row) + 0.5f - cy);
            if (dist > kRadius) {
                continue;
            }
            // Soft falloff from the pointer to the splat edge.
            const float f = 1.0f - dist / kRadius;
            auto& cell = m_cells[static_cast<size_t>(row) * static_cast<size_t>(m_cols) + static_cast<size_t>(col)];
            cell = juce::jmax(cell, f * f);
        }
    }
    if (!isTimerRunning()) {
        startTimerHz(30);
    }
}

void PixelCanvas::energize(float level)
{
    if (m_cells.empty() || level < 0.02f) {
        return;
    }
    // Perceptual-ish scaling: quiet audio sparkles a little, loud a lot.
    const int count = 1 + static_cast<int>(level * level * 28.0f);
    for (int i = 0; i < count; ++i) {
        const int col = m_random.nextInt(m_cols);
        const int row = m_random.nextInt(m_rows);
        auto& cell = m_cells[static_cast<size_t>(row) * static_cast<size_t>(m_cols) + static_cast<size_t>(col)];
        cell = juce::jmax(cell, level * (0.35f + 0.65f * m_random.nextFloat()));
    }
    if (!isTimerRunning()) {
        startTimerHz(30);
    }
}

void PixelCanvas::timerCallback()
{
    bool anyAlive = false;
    for (auto& c : m_cells) {
        if (c > 0.0f) {
            c = juce::jmax(0.0f, c - kDecay);
            anyAlive = anyAlive || c > 0.0f;
        }
    }
    if (!anyAlive) {
        stopTimer();
    }
    repaint();
}

void PixelCanvas::paint(juce::Graphics& g)
{
    // Colour sweeps cool grey -> amber as a cell brightens; squares sit on the
    // kGap grid with a 2 px gutter.
    const auto cool = juce::Colour(0xff39414c);
    const auto hot = theme::colours::accent;
    for (int row = 0; row < m_rows; ++row) {
        for (int col = 0; col < m_cols; ++col) {
            const float v = m_cells[static_cast<size_t>(row) * static_cast<size_t>(m_cols) + static_cast<size_t>(col)];
            if (v < 0.02f) {
                continue;
            }
            g.setColour(cool.interpolatedWith(hot, v).withAlpha(v * kMaxAlpha));
            g.fillRect(col * kGap + 1, row * kGap + 1, kGap - 2, kGap - 2);
        }
    }
}

} // namespace tapein::app
