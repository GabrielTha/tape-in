// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tapein::app {

/// A horizontal stereo meter over a −60..0 dBFS scale: an RMS bar over a
/// translucent peak bar, both filled through green/amber/red zones, with a
/// peak-hold tick and a clip light that stays lit for 1.5 s after a full-scale
/// peak. Fed linear 0..1 values once per UI tick (~30 Hz); peak attacks
/// instantly and releases steadily, RMS is averaged. A dimmed meter (muted or
/// solo-excluded channel) clears immediately and paints faded.
class LevelMeter final : public juce::Component {
public:
    /// Linear peaks per channel plus one linear RMS for both; a negative RMS
    /// derives a VU-like average from the peaks instead.
    void setLevels(float peakL, float peakR, float rms = -1.0f);
    void setDimmed(bool dimmed);

    void paint(juce::Graphics& g) override;

private:
    struct Channel {
        float peak = 0.0f; ///< ballistic peak (linear)
        float rms = 0.0f; ///< averaged level (linear)
        float hold = 0.0f; ///< peak-hold value (linear)
        int holdAge = 0; ///< ticks since the hold was last raised
    };

    void feed(Channel& ch, float peak, float rms);
    void drawTrack(juce::Graphics& g, juce::Rectangle<float> area, const Channel& ch) const;

    Channel m_left;
    Channel m_right;
    int m_clipAge = 1000; ///< ticks since the last full-scale peak
    bool m_dimmed = false;
};

} // namespace tapein::app
