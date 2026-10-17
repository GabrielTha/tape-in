// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "LevelMeter.h"

#include "tapein/core/session/engine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tapein::app {

/// One row: name, state, meter, fader, mute and solo. Edits call back out.
class SourceRow final : public juce::Component {
public:
    SourceRow();

    void update(const core::session::SourceSnapshot& snap);
    [[nodiscard]] uint64_t sourceId() const noexcept { return m_sourceId; }

    std::function<void(uint64_t, float)> onGain;
    std::function<void(uint64_t, bool)> onMute;
    std::function<void(uint64_t, bool)> onSolo;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    uint64_t m_sourceId = 0;
    bool m_live = true;
    juce::Label m_name;
    juce::Label m_state;
    LevelMeter m_meter;
    juce::Slider m_gain;
    juce::TextButton m_mute{"M"};
    juce::TextButton m_solo{"S"};
};

} // namespace tapein::app
