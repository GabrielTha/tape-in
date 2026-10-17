// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tapein::app {

/// The About box: name, version, the no-affiliation notice, the licence and
/// the source links. Shown from the tray menu.
class AboutComponent final : public juce::Component {
public:
    AboutComponent();
    void resized() override;
    void paint(juce::Graphics& g) override;

    static void show();

private:
    juce::Label m_title;
    juce::Label m_body;
    juce::HyperlinkButton m_source;
    juce::HyperlinkButton m_notices;
};

} // namespace tapein::app
