// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "LevelMeter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tapein::app {

/// The master channel strip under the record bar: a "MASTER" label, the
/// stereo output meter, a dB readout of the mix peak and a master mute.
/// Muting clears and dims the meter immediately and shows "MUTED".
class MasterStrip final : public juce::Component {
public:
    MasterStrip();

    /// Linear 0..1 output peaks, once per UI tick.
    void setLevels(float peakL, float peakR);

    std::function<void(bool)> onMute;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    [[nodiscard]] juce::String readoutText() const;

    LevelMeter m_meter;
    juce::TextButton m_mute{"M"};
    float m_peak = 0.0f; ///< smoothed max of both channels, for the readout
    bool m_muted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStrip)
};

} // namespace tapein::app
