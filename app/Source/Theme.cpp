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

const juce::Image& logo()
{
    static juce::Image img =
        juce::ImageFileFormat::loadFrom(BinaryData::tapein_logo_png, BinaryData::tapein_logo_pngSize);
    return img;
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

namespace icons {

namespace {
// Maps a path authored in a 24x24 viewBox onto `box`.
juce::AffineTransform fit(juce::Rectangle<float> box)
{
    return juce::AffineTransform::scale(box.getWidth() / 24.0f, box.getHeight() / 24.0f)
        .translated(box.getX(), box.getY());
}
void strokeVb(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, float stroke, juce::Path p)
{
    p.applyTransform(fit(box));
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
void fillVb(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, juce::Path p)
{
    p.applyTransform(fit(box));
    g.setColour(c);
    g.fillPath(p);
}
} // namespace

void reel(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, float stroke)
{
    juce::Path p;
    p.addEllipse(12 - 8.5f, 12 - 8.5f, 17, 17);
    p.addEllipse(12 - 2, 12 - 2, 4, 4);
    strokeVb(g, box, c, stroke, p);
}

void speakerLow(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, float stroke)
{
    juce::Path p; // M4 9v6h4l5 4V5L8 9z
    p.startNewSubPath(4, 9);
    p.lineTo(4, 15);
    p.lineTo(8, 15);
    p.lineTo(13, 19);
    p.lineTo(13, 5);
    p.lineTo(8, 9);
    p.closeSubPath();
    strokeVb(g, box, c, stroke, p);
}

void speakerHigh(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, float stroke)
{
    speakerLow(g, box, c, stroke);
    juce::Path p; // two sound arcs
    p.addCentredArc(13.0f, 12.0f, 3.5f, 4.0f, 0.0f, 0.1f, 3.04f, true);
    p.addCentredArc(13.0f, 12.0f, 6.0f, 6.5f, 0.0f, 0.2f, 2.94f, true);
    strokeVb(g, box, c, stroke, p);
}

void gear(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, float stroke)
{
    // A proper cog: a toothed ring (teeth on the outer rim, flat tops and
    // valleys between them) around a central hole — not a sun.
    juce::Path p;
    constexpr int teeth = 8;
    const float pitch = juce::MathConstants<float>::twoPi / static_cast<float>(teeth);
    const float cx = 12.0f, cy = 12.0f;
    const float frac[] = {-0.30f, -0.17f, 0.17f, 0.30f}; // valley, tip, tip, valley
    const float radius[] = {6.6f, 9.5f, 9.5f, 6.6f};     // inner (root), outer (tip)
    for (int i = 0; i < teeth; ++i) {
        for (int k = 0; k < 4; ++k) {
            const float a = (static_cast<float>(i) + frac[k]) * pitch;
            const float x = cx + std::cos(a) * radius[k];
            const float y = cy + std::sin(a) * radius[k];
            if (i == 0 && k == 0)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
        }
    }
    p.closeSubPath();
    p.addEllipse(cx - 3.0f, cy - 3.0f, 6.0f, 6.0f); // center hole
    strokeVb(g, box, c, stroke, p);
}

void chevron(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, bool up, float stroke)
{
    juce::Path p;
    if (up) {
        p.startNewSubPath(6, 15);
        p.lineTo(12, 9);
        p.lineTo(18, 15);
    } else {
        p.startNewSubPath(6, 9);
        p.lineTo(12, 15);
        p.lineTo(18, 9);
    }
    strokeVb(g, box, c, stroke, p);
}

void check(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, float stroke)
{
    juce::Path p; // M5 12l5 5 9-11
    p.startNewSubPath(5, 12);
    p.lineTo(10, 17);
    p.lineTo(19, 6);
    strokeVb(g, box, c, stroke, p);
}

void stack(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, float stroke)
{
    juce::Path p;
    p.addRoundedRectangle(4.0f, 7.0f, 12.0f, 12.0f, 2.0f);
    p.startNewSubPath(8, 7); // M8 7V5a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-2
    p.lineTo(8, 5);
    p.quadraticTo(8, 3, 10, 3);
    p.lineTo(18, 3);
    p.quadraticTo(20, 3, 20, 5);
    p.lineTo(20, 13);
    p.quadraticTo(20, 15, 18, 15);
    p.lineTo(16, 15);
    strokeVb(g, box, c, stroke, p);
}

void prev(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, float stroke)
{
    juce::Path bar; // vertical bar at x=6
    bar.startNewSubPath(6, 5.5f);
    bar.lineTo(6, 18.5f);
    strokeVb(g, box, c, stroke, bar);
    juce::Path tri; // M19 6 9 12l10 6z
    tri.startNewSubPath(19, 6);
    tri.lineTo(9, 12);
    tri.lineTo(19, 18);
    tri.closeSubPath();
    fillVb(g, box, c, tri);
}

void next(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c, float stroke)
{
    juce::Path tri; // M5 6l10 6-10 6z
    tri.startNewSubPath(5, 6);
    tri.lineTo(15, 12);
    tri.lineTo(5, 18);
    tri.closeSubPath();
    fillVb(g, box, c, tri);
    juce::Path bar; // vertical bar at x=18
    bar.startNewSubPath(18, 5.5f);
    bar.lineTo(18, 18.5f);
    strokeVb(g, box, c, stroke, bar);
}

void play(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c)
{
    juce::Path p; // M7 5 19 12 7 19z
    p.startNewSubPath(7, 5);
    p.lineTo(19, 12);
    p.lineTo(7, 19);
    p.closeSubPath();
    fillVb(g, box, c, p);
}

void pause(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour c)
{
    juce::Path p;
    p.addRoundedRectangle(7.5f, 5.0f, 2.6f, 14.0f, 1.0f);
    p.addRoundedRectangle(13.9f, 5.0f, 2.6f, 14.0f, 1.0f);
    fillVb(g, box, c, p);
}

} // namespace icons

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
