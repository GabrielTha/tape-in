// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "SourceRow.h"

#include "Theme.h"

namespace tapein::app {

using core::session::TransportCommand;

// A small round ghost button that paints a stroked transport glyph.
class SourceRow::IconButton final : public juce::Button {
public:
    enum class Glyph { prev, play, pause, next };

    explicit IconButton(Glyph g) : juce::Button({}), m_glyph(g) {}
    void setGlyph(Glyph g)
    {
        m_glyph = g;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        const bool primary = m_glyph == Glyph::play || m_glyph == Glyph::pause;
        if (down) {
            theme::drawInset(g, b.reduced(1.0f), b.getWidth() * 0.5f, theme::colours::slot);
        } else {
            theme::drawRaised(g, b.reduced(2.0f), b.getWidth() * 0.5f - 2.0f, theme::colours::slot);
        }
        juce::Colour ink = primary ? theme::colours::accent : theme::colours::muted;
        if (!isEnabled()) {
            ink = theme::colours::dim;
        } else if (highlighted) {
            ink = ink.brighter(0.3f);
        }
        g.setColour(ink);

        auto c = b.getCentre();
        const float s = b.getWidth() * 0.18f;
        juce::Path p;
        switch (m_glyph) {
        case Glyph::prev:
            g.drawLine(c.x - s * 1.4f, c.y - s, c.x - s * 1.4f, c.y + s, 1.8f);
            p.addTriangle(c.x + s, c.y - s, c.x + s, c.y + s, c.x - s * 0.7f, c.y);
            g.fillPath(p);
            break;
        case Glyph::next:
            g.drawLine(c.x + s * 1.4f, c.y - s, c.x + s * 1.4f, c.y + s, 1.8f);
            p.addTriangle(c.x - s, c.y - s, c.x - s, c.y + s, c.x + s * 0.7f, c.y);
            g.fillPath(p);
            break;
        case Glyph::play:
            p.addTriangle(c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s, c.y);
            g.fillPath(p);
            break;
        case Glyph::pause:
            g.fillRoundedRectangle(c.x - s * 0.9f, c.y - s, s * 0.6f, s * 2.0f, 1.0f);
            g.fillRoundedRectangle(c.x + s * 0.3f, c.y - s, s * 0.6f, s * 2.0f, 1.0f);
            break;
        }
    }

private:
    Glyph m_glyph;
};

namespace {
const char* stateText(core::dsp::StreamState s, bool live)
{
    if (!live) {
        return "Disconnected";
    }
    switch (s) {
    case core::dsp::StreamState::buffering:
        return "Buffering";
    case core::dsp::StreamState::playing:
    case core::dsp::StreamState::fadingOut:
        return "Playing";
    case core::dsp::StreamState::refilling:
        return "Rebuffering";
    case core::dsp::StreamState::stopped:
        return "Stopped";
    }
    return "";
}
} // namespace

SourceRow::SourceRow()
{
    m_title.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    m_title.setColour(juce::Label::textColourId, theme::colours::text);
    m_title.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(m_title);

    m_subtitle.setFont(juce::FontOptions(12.0f));
    m_subtitle.setColour(juce::Label::textColourId, theme::colours::muted);
    addAndMakeVisible(m_subtitle);

    m_prev = std::make_unique<IconButton>(IconButton::Glyph::prev);
    m_play = std::make_unique<IconButton>(IconButton::Glyph::play);
    m_next = std::make_unique<IconButton>(IconButton::Glyph::next);
    m_prev->onClick = [this] {
        if (onTransport)
            onTransport(m_sourceId, TransportCommand::previous);
    };
    m_play->onClick = [this] {
        if (onTransport)
            onTransport(m_sourceId, TransportCommand::playPause);
    };
    m_next->onClick = [this] {
        if (onTransport)
            onTransport(m_sourceId, TransportCommand::next);
    };
    addAndMakeVisible(*m_prev);
    addAndMakeVisible(*m_play);
    addAndMakeVisible(*m_next);

    m_volume.setSliderStyle(juce::Slider::LinearHorizontal);
    m_volume.setRange(-60.0, 12.0, 0.1);
    m_volume.setValue(0.0, juce::dontSendNotification);
    m_volume.setDoubleClickReturnValue(true, 0.0);
    m_volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    m_volume.onValueChange = [this] {
        if (onGain)
            onGain(m_sourceId, static_cast<float>(m_volume.getValue()));
    };
    addAndMakeVisible(m_volume);

    auto setupTag = [this](juce::TextButton& b) {
        b.setClickingTogglesState(true);
        b.setColour(juce::TextButton::buttonColourId, theme::colours::slot);
        b.setColour(juce::TextButton::buttonOnColourId, theme::colours::accent);
        b.setColour(juce::TextButton::textColourOffId, theme::colours::faint);
        b.setColour(juce::TextButton::textColourOnId, theme::colours::background);
        addAndMakeVisible(b);
    };
    setupTag(m_solo);
    setupTag(m_mute);
    m_solo.onClick = [this] {
        if (onSolo)
            onSolo(m_sourceId, m_solo.getToggleState());
    };
    m_mute.onClick = [this] {
        if (onMute)
            onMute(m_sourceId, m_mute.getToggleState());
    };
}

SourceRow::~SourceRow() = default;

void SourceRow::update(const core::session::SourceSnapshot& snap)
{
    m_sourceId = snap.sourceId;
    m_live = snap.live;
    m_playing = snap.state == core::dsp::StreamState::playing || snap.state == core::dsp::StreamState::fadingOut;

    // Title: track name if we have metadata, else the client/device name.
    const juce::String device = snap.clientName.empty() ? juce::String(snap.peer) : juce::String(snap.clientName);
    juce::String title = juce::String(snap.trackName);
    juce::String subtitle;
    if (title.isEmpty()) {
        title = device;
        subtitle = stateText(snap.state, snap.live);
    } else {
        subtitle = juce::String(snap.artist);
        if (!subtitle.isEmpty())
            subtitle += " \xc2\xb7 ";
        subtitle += device;
    }
    m_title.setText(title, juce::dontSendNotification);
    m_subtitle.setText(subtitle, juce::dontSendNotification);

    m_stateText = stateText(snap.state, snap.live);
    m_stateColour = !snap.live ? theme::colours::faint
                   : m_playing ? theme::colours::playing
                               : theme::colours::muted;

    if (m_play)
        m_play->setGlyph(m_playing ? IconButton::Glyph::pause : IconButton::Glyph::play);

    // Decode artwork only when the bytes change.
    if (snap.artwork.size() != m_artworkSize) {
        m_artworkSize = snap.artwork.size();
        m_artwork = snap.artwork.empty()
                        ? juce::Image()
                        : juce::ImageFileFormat::loadFrom(snap.artwork.data(), snap.artwork.size());
        repaint();
    }

    if (std::abs(m_volume.getValue() - snap.userGainDb) > 0.05 && !m_volume.isMouseButtonDown())
        m_volume.setValue(snap.userGainDb, juce::dontSendNotification);
    m_mute.setToggleState(snap.mute, juce::dontSendNotification);
    m_solo.setToggleState(snap.solo, juce::dontSendNotification);

    const bool enabled = snap.live;
    m_prev->setEnabled(enabled);
    m_play->setEnabled(enabled);
    m_next->setEnabled(enabled);
    setAlpha(snap.live ? 1.0f : 0.68f);
}

void SourceRow::resized()
{
    auto r = getLocalBounds().reduced(14, 12);
    auto art = r.removeFromLeft(56);
    juce::ignoreUnused(art); // painted directly
    r.removeFromLeft(13);
    auto sm = r.removeFromRight(32);
    m_solo.setBounds(sm.removeFromTop(26));
    sm.removeFromTop(6);
    m_mute.setBounds(sm.removeFromTop(26));
    r.removeFromRight(10);

    auto top = r.removeFromTop(38);
    m_title.setBounds(top.removeFromTop(20));
    m_subtitle.setBounds(top);

    auto ctrls = r.removeFromBottom(34);
    m_prev->setBounds(ctrls.removeFromLeft(32));
    ctrls.removeFromLeft(8);
    m_play->setBounds(ctrls.removeFromLeft(38));
    ctrls.removeFromLeft(8);
    m_next->setBounds(ctrls.removeFromLeft(32));
    ctrls.removeFromLeft(14);
    ctrls.removeFromLeft(18); // speaker glyph (painted)
    m_volume.setBounds(ctrls);
}

void SourceRow::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(3.0f);
    theme::drawRaised(g, b, 16.0f);

    // Artwork well.
    auto art = juce::Rectangle<float>(14.0f, 12.0f, 56.0f, 56.0f);
    theme::drawInset(g, art, 13.0f);
    if (m_artwork.isValid()) {
        juce::Path clip;
        clip.addRoundedRectangle(art.reduced(1.0f), 12.0f);
        g.saveState();
        g.reduceClipRegion(clip);
        g.drawImage(m_artwork, art.reduced(1.0f), juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        g.restoreState();
    } else {
        g.setColour(theme::colours::faint);
        const auto c = art.getCentre();
        g.drawEllipse(c.x - 12, c.y - 12, 24, 24, 1.4f);
        g.drawEllipse(c.x - 3, c.y - 3, 6, 6, 1.4f);
    }

    // State pill, top-right (before the S/M column).
    if (m_stateText.isNotEmpty()) {
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        const float tw = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), m_stateText);
        auto pill = juce::Rectangle<float>(getWidth() - 46.0f - tw - 22.0f, 14.0f, tw + 22.0f, 20.0f);
        theme::drawInset(g, pill, 10.0f, theme::colours::inset);
        g.setColour(m_stateColour);
        g.fillEllipse(pill.getX() + 8.0f, pill.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.drawText(m_stateText, pill.withTrimmedLeft(18.0f), juce::Justification::centredLeft);
    }

    // Speaker glyph left of the volume slider.
    const auto vb = m_volume.getBounds().toFloat();
    g.setColour(theme::colours::faint);
    juce::Path spk;
    const float sx = vb.getX() - 16.0f;
    const float sy = vb.getCentreY();
    spk.addTriangle(sx, sy - 2, sx + 4, sy - 2, sx + 7, sy - 6);
    spk.addRectangle(sx, sy - 2, 4.0f, 4.0f);
    g.fillPath(spk);
    g.strokePath(spk, juce::PathStrokeType(1.2f));
}

} // namespace tapein::app
