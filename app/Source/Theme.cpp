// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "Theme.h"

#include "BinaryData.h"

namespace tapein::app::theme {

juce::Typeface::Ptr boldonse()
{
    static juce::Typeface::Ptr tf =
        juce::Typeface::createSystemTypefaceFor(BinaryData::Boldonse_ttf, BinaryData::Boldonse_ttfSize);
    return tf;
}

juce::Font boldonseFont(float height)
{
    return juce::Font(boldonse()).withHeight(height);
}

void drawRaised(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour fill)
{
    // Dark shadow bottom-right.
    {
        juce::DropShadow shadow(colours::shadowDark, 10, {4, 5});
        juce::Path p;
        p.addRoundedRectangle(bounds, radius);
        shadow.drawForPath(g, p);
    }
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, radius);
    // Light highlight along the top edge.
    g.setColour(colours::shadowLight);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

void drawInset(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour fill)
{
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, radius);
    // Fake an inner shadow with a darker inner stroke at the top-left.
    g.setColour(colours::shadowDark.withAlpha(0.45f));
    juce::Path top;
    top.startNewSubPath(bounds.getX() + radius, bounds.getY() + 1.0f);
    top.lineTo(bounds.getRight() - radius, bounds.getY() + 1.0f);
    g.strokePath(top, juce::PathStrokeType(1.5f));
    g.setColour(colours::shadowLight);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

NeumorphicLookAndFeel::NeumorphicLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, colours::background);
    setColour(juce::Label::textColourId, colours::text);
    setColour(juce::Slider::trackColourId, colours::accent);
    setColour(juce::Slider::backgroundColourId, colours::inset);
    setColour(juce::Slider::thumbColourId, colours::slot);
    setColour(juce::TextButton::buttonColourId, colours::slot);
    setColour(juce::TextButton::textColourOffId, colours::muted);
    setColour(juce::TextButton::textColourOnId, colours::accent);
    setColour(juce::ComboBox::backgroundColourId, colours::slot);
    setColour(juce::ComboBox::textColourId, colours::text);
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0x11ffffff));
    setColour(juce::ComboBox::arrowColourId, colours::faint);
    setColour(juce::PopupMenu::backgroundColourId, colours::slot);
    setColour(juce::PopupMenu::textColourId, colours::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff20242a));
    setColour(juce::TextEditor::backgroundColourId, colours::inset);
    setColour(juce::TextEditor::textColourId, colours::text);
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0x11ffffff));
    setColour(juce::ToggleButton::textColourId, colours::muted);
    setColour(juce::ToggleButton::tickColourId, colours::accent);
}

void NeumorphicLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                             float /*minPos*/, float /*maxPos*/, juce::Slider::SliderStyle /*style*/,
                                             juce::Slider& /*slider*/)
{
    const auto track = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y) + static_cast<float>(height) * 0.5f - 3.0f,
                                              static_cast<float>(width), 6.0f);
    drawInset(g, track, 3.0f);
    const float fillW = juce::jlimit(0.0f, static_cast<float>(width), sliderPos - static_cast<float>(x));
    g.setColour(colours::accent);
    g.fillRoundedRectangle(track.withWidth(fillW), 3.0f);
    g.setColour(colours::slot);
    const float knobR = 7.0f;
    g.fillEllipse(sliderPos - knobR, track.getCentreY() - knobR, knobR * 2, knobR * 2);
    g.setColour(colours::shadowLight);
    g.drawEllipse(sliderPos - knobR, track.getCentreY() - knobR, knobR * 2, knobR * 2, 1.0f);
}

} // namespace tapein::app::theme
