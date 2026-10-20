// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace tapein::app {

/// A decorative pixel grid behind the whole window: cells light up under the
/// pointer and fade out, their colour sweeping from a faint cool grey to the
/// amber accent as they decay. Never intercepts the mouse; the parent feeds
/// it pointer positions (so moves over child components still paint).
class PixelCanvas final : public juce::Component, private juce::Timer {
public:
    PixelCanvas();

    /// Light the cells around a pointer position, in this component's coords.
    void pointerAt(juce::Point<float> pos);

    /// Scatter sparkles across the grid from the master output level (linear
    /// 0..1 peak): louder means more and brighter cells. Call per UI tick.
    void energize(float level);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    int m_cols = 0;
    int m_rows = 0;
    std::vector<float> m_cells; ///< per-cell intensity, 0..1
    juce::Random m_random;      ///< sparkle placement; message thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PixelCanvas)
};

} // namespace tapein::app
