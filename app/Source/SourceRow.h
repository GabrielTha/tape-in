// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include "tapein/core/session/dacp_client.h"
#include "tapein/core/session/engine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tapein::app {

/// A neumorphic slot: album artwork, now-playing metadata, transport buttons
/// (prev / play-pause / next sent back to the sender), a volume fader (local
/// mix gain) and solo/mute. Edits call back out to the engine.
class SourceRow final : public juce::Component {
public:
    SourceRow();
    ~SourceRow() override;

    void update(const core::session::SourceSnapshot& snap);
    [[nodiscard]] uint64_t sourceId() const noexcept { return m_sourceId; }

    std::function<void(uint64_t, float)> onGain;
    std::function<void(uint64_t, bool)> onMute;
    std::function<void(uint64_t, bool)> onSolo;
    std::function<void(uint64_t, core::session::TransportCommand)> onTransport;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class IconButton; ///< small ghost transport button

    uint64_t m_sourceId = 0;
    bool m_live = true;
    bool m_playing = false;
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
    juce::TextButton m_solo{"S"};
    juce::TextButton m_mute{"M"};
};

} // namespace tapein::app
