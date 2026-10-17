// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tapein::app {

/// A horizontal stereo peak meter with a short decay. Fed linear 0..1 peaks.
class LevelMeter final : public juce::Component {
public:
    void setLevels(float peakL, float peakR);
    void paint(juce::Graphics& g) override;

private:
    [[nodiscard]] float decayed(float current, float& held) const;
    void drawBar(juce::Graphics& g, juce::Rectangle<float> area, float level) const;

    float m_heldL = 0.0f;
    float m_heldR = 0.0f;
};

} // namespace tapein::app
