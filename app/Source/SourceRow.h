// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "LevelMeter.h"

#include "tapein/core/session/dacp_client.h"
#include "tapein/core/session/engine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tapein::app {

/// A neumorphic slot (the Main/Empty artboards): album artwork, now-playing
/// metadata, a now-playing state tag, transport buttons (prev / play-pause /
/// next sent back to the sender), a volume fader (local mix gain) and
/// solo/mute. `update()` shows a live source; `setEmpty()` shows the "waiting
/// for a device" placeholder. Edits call back out to the engine.
class SourceRow final : public juce::Component {
public:
    SourceRow();
    ~SourceRow() override;

    /// `audible` is false when the slot is muted or excluded by another
    /// slot's solo; its meter then clears and dims, like a console strip.
    void update(const core::session::SourceSnapshot& snap, bool audible);
    void setEmpty();
    [[nodiscard]] uint64_t sourceId() const noexcept { return m_sourceId; }

    std::function<void(uint64_t, float)> onGain;
    std::function<void(uint64_t, bool)> onMute;
    std::function<void(uint64_t, bool)> onSolo;
    std::function<void(uint64_t, core::session::TransportCommand)> onTransport;
    std::function<void(uint64_t)> onApprove; ///< allow a pending connection
    std::function<void(uint64_t)> onDecline; ///< reject a pending connection

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class IconButton; ///< small ghost transport button

    void applyControlVisibility(); ///< transport vs. Allow/Decline, by pending state

    uint64_t m_sourceId = 0;
    bool m_empty = false;
    bool m_live = true;
    bool m_playing = false;
    bool m_pending = false; ///< awaiting the user's Allow/Decline
    juce::String m_stateText;
    juce::Colour m_stateColour;

    juce::Image m_artwork;
    size_t m_artworkSize = 0;

    juce::Label m_title;
    juce::Label m_subtitle;

    std::unique_ptr<IconButton> m_prev;
    std::unique_ptr<IconButton> m_play;
    std::unique_ptr<IconButton> m_next;
    juce::Slider m_volume;
    LevelMeter m_meter;
    juce::TextButton m_solo{"S"};
    juce::TextButton m_mute{"M"};
    juce::TextButton m_allow{"Allow"};
    juce::TextButton m_decline{"Decline"};
};

} // namespace tapein::app
