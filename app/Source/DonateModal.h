// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tapein::app {

/// The donate overlay: a scrim and a neumorphic card with the maintainer's
/// avatar, the open-source credit and the donation link. Opened from the
/// footer's DONATE button; a click on the scrim closes it.
class DonateModal final : public juce::Component {
public:
    DonateModal();
    ~DonateModal() override;

    std::function<void()> onClose;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    class AccentButton; ///< raised amber pill that opens the donation page

    [[nodiscard]] juce::Rectangle<int> cardBounds() const;

    juce::Image m_avatar;
    std::unique_ptr<AccentButton> m_donate;
    juce::HyperlinkButton m_gabriel; ///< Instagram links on the two names
    juce::HyperlinkButton m_adam;
    juce::HyperlinkButton m_link;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DonateModal)
};

} // namespace tapein::app
