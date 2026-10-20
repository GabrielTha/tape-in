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
inline const juce::Colour muted{0xffa0a7b0};
inline const juce::Colour faint{0xff8b939d};
inline const juce::Colour dim{0xff6b747f};
inline const juce::Colour dimmer{0xff6e7680};
inline const juce::Colour subtitle{0xffa0a7b0};
inline const juce::Colour scrim{0x9908090b}; // rgba(8,9,11,0.6)
inline const juce::Colour hover{0xff20242a};
inline const juce::Colour playing{0xff5fcf86};
inline const juce::Colour shadowDark{0x8c000000};
inline const juce::Colour shadowLight{0x0affffff};
} // namespace colours

/// The embedded Boldonse display typeface (brand wordmark, section titles).
juce::Typeface::Ptr boldonse();
juce::Font boldonseFont(float height);

/// The embedded "TAPE IN" wordmark logo (transparent PNG, cassette artwork).
const juce::Image& logo();

/// A raised neumorphic panel: fill plus a soft dark shadow bottom-right and a
/// light highlight top-left. `bounds` should leave a few px of margin for the
/// shadow.
void drawRaised(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour fill = colours::slot);

/// A recessed (inset) well: dark inner shadow top-left, light bottom-right.
void drawInset(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour fill = colours::inset);

/// Line/fill icons transcribed from the design's 24x24 SVGs, each drawn to
/// fill `box` (assumed square) in the given colour. Stroke width is in box px.
namespace icons {
void reel(juce::Graphics&, juce::Rectangle<float> box, juce::Colour, float stroke = 1.6f);
void speakerLow(juce::Graphics&, juce::Rectangle<float> box, juce::Colour, float stroke = 1.7f);
void speakerHigh(juce::Graphics&, juce::Rectangle<float> box, juce::Colour, float stroke = 1.7f);
void gear(juce::Graphics&, juce::Rectangle<float> box, juce::Colour, float stroke = 1.7f);
void chevron(juce::Graphics&, juce::Rectangle<float> box, juce::Colour, bool up, float stroke = 2.0f);
void check(juce::Graphics&, juce::Rectangle<float> box, juce::Colour, float stroke = 2.2f);
void stack(juce::Graphics&, juce::Rectangle<float> box, juce::Colour, float stroke = 1.5f);
void prev(juce::Graphics&, juce::Rectangle<float> box, juce::Colour, float stroke = 1.7f);
void next(juce::Graphics&, juce::Rectangle<float> box, juce::Colour, float stroke = 1.7f);
void play(juce::Graphics&, juce::Rectangle<float> box, juce::Colour);
void pause(juce::Graphics&, juce::Rectangle<float> box, juce::Colour);
void dragOut(juce::Graphics&, juce::Rectangle<float> box, juce::Colour, float stroke = 1.8f); ///< share/export tray
} // namespace icons

/// App-wide LookAndFeel: dark neumorphic colours for sliders, buttons, combos.
class NeumorphicLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    NeumorphicLookAndFeel();

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minPos,
                          float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) override;
};

} // namespace tapein::app::theme
