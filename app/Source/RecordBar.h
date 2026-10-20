// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace tapein::app {

/// The global sampling control: a horizontal neumorphic bar that sits above
/// the buffer footer. A round record/stop button on the left, a live input
/// waveform in the middle (the whole bar is the drag source once a sample is
/// written), and an elapsed/status readout on the right. Recording is global,
/// capturing the clean mix of every source, not a single card.
class RecordBar final : public juce::Component {
public:
    RecordBar();
    ~RecordBar() override;

    /// Toggle the recording on/off; the argument is the new desired state.
    std::function<void(bool start)> onToggle;

    /// Live recording state + elapsed seconds, from the engine (30 Hz poll).
    void setRecording(bool recording, int elapsedSeconds);
    /// The rolling input envelope (0..1, oldest → newest) for the waveform.
    void setWaveform(std::vector<float> envelope);
    /// The most recent sample written to disk; when valid, the bar is draggable.
    void setSample(const juce::File& file);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    class RecButton; ///< the round record/stop disc

    [[nodiscard]] juce::Rectangle<int> waveBounds() const;
    [[nodiscard]] juce::String readoutText() const;
    [[nodiscard]] int readoutWidth() const;

    std::unique_ptr<RecButton> m_rec;
    std::vector<float> m_wave;
    bool m_recording = false;
    int m_elapsed = 0;
    juce::File m_sample;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordBar)
};

} // namespace tapein::app
