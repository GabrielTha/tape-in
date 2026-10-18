// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Neumorphic palette, the Boldonse brand typeface, soft-shadow draw helpers
// and the app's LookAndFeel. The visual language of the Phase 3 UI.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tapein::app::theme {

namespace colours {
inline const juce::Colour background{0xff16181c};
inline const juce::Colour slot{0xff1a1d22};
inline const juce::Colour inset{0xff15181c};
inline const juce::Colour accent{0xffe0a35c};
inline const juce::Colour accentPress{0xffd4914a};
inline const juce::Colour text{0xffe9ebee};
inline const juce::Colour muted{0xff9097a0};
inline const juce::Colour faint{0xff6d7680};
inline const juce::Colour dim{0xff454d57};
inline const juce::Colour playing{0xff5fcf86};
inline const juce::Colour shadowDark{0x8c000000};
inline const juce::Colour shadowLight{0x0affffff};
} // namespace colours

/// The embedded Boldonse display typeface (brand wordmark, section titles).
juce::Typeface::Ptr boldonse();
juce::Font boldonseFont(float height);

/// A raised neumorphic panel: fill plus a soft dark shadow bottom-right and a
/// light highlight top-left. `bounds` should leave a few px of margin for the
/// shadow.
void drawRaised(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour fill = colours::slot);

/// A recessed (inset) well: dark inner shadow top-left, light bottom-right.
void drawInset(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour fill = colours::inset);

/// App-wide LookAndFeel: dark neumorphic colours for sliders, buttons, combos.
class NeumorphicLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    NeumorphicLookAndFeel();

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minPos,
                          float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) override;
};

} // namespace tapein::app::theme
