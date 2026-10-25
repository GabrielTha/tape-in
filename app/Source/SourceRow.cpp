// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "SourceRow.h"

#include "Theme.h"

namespace tapein::app {

using core::session::TransportCommand;

// A round ghost transport button that paints a neumorphic disc plus a glyph.
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
        if (down && isEnabled()) {
            theme::drawInset(g, b.reduced(1.0f), b.getWidth() * 0.5f, theme::colours::slot);
        } else {
            theme::drawRaised(g, b.reduced(2.0f), b.getWidth() * 0.5f - 2.0f, theme::colours::slot);
        }
        juce::Colour ink = primary ? theme::colours::accent : juce::Colour(0xffc3c9d0);
        if (!isEnabled()) {
            ink = theme::colours::dimmer;
        } else if (highlighted) {
            ink = primary ? ink.brighter(0.15f) : juce::Colours::white;
        }

        // Glyph box is ~55% of the disc, centred.
        const float s = b.getWidth() * 0.52f;
        auto box = juce::Rectangle<float>(s, s).withCentre(b.getCentre());
        switch (m_glyph) {
        case Glyph::prev:
            theme::icons::prev(g, box, ink);
            break;
        case Glyph::next:
            theme::icons::next(g, box, ink);
            break;
        case Glyph::play:
            theme::icons::play(g, box, ink);
            break;
        case Glyph::pause:
            theme::icons::pause(g, box, ink);
            break;
        }
    }

private:
    Glyph m_glyph;
};

namespace {
// Last-resort source name from the peer endpoint: "[fe80::1]:64368" -> "fe80::1".
juce::String prettyPeer(const std::string& peer)
{
    juce::String p(peer);
    if (p.startsWith("[")) {
        return p.fromFirstOccurrenceOf("[", false, false).upToFirstOccurrenceOf("]", false, false);
    }
    return p.upToLastOccurrenceOf(":", false, false);
}

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
    m_subtitle.setColour(juce::Label::textColourId, theme::colours::subtitle);
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

    addAndMakeVisible(m_meter);

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

    // Shown only while a connection is pending approval (ask-before-accept).
    m_allow.setColour(juce::TextButton::buttonColourId, theme::colours::accent);
    m_allow.setColour(juce::TextButton::textColourOffId, theme::colours::background);
    m_allow.onClick = [this] {
        if (onApprove)
            onApprove(m_sourceId);
    };
    addChildComponent(m_allow);

    m_decline.setColour(juce::TextButton::buttonColourId, theme::colours::slot);
    m_decline.setColour(juce::TextButton::textColourOffId, theme::colours::muted);
    m_decline.onClick = [this] {
        if (onDecline)
            onDecline(m_sourceId);
    };
    addChildComponent(m_decline);
}

void SourceRow::applyControlVisibility()
{
    const bool controls = !m_pending; // empty slots still show the (disabled) transport
    m_meter.setVisible(controls && !m_empty);
    m_prev->setVisible(controls);
    m_play->setVisible(controls);
    m_next->setVisible(controls);
    m_volume.setVisible(controls);
    m_solo.setVisible(controls);
    m_mute.setVisible(controls);
    m_allow.setVisible(m_pending);
    m_decline.setVisible(m_pending);
}

SourceRow::~SourceRow() = default;

void SourceRow::setEmpty()
{
    m_empty = true;
    m_pending = false;
    m_sourceId = 0;
    m_live = false;
    m_playing = false;
    m_stateText.clear();
    m_artwork = juce::Image();
    m_artworkSize = 0;
    m_title.setText("Slot", juce::dontSendNotification);
    m_title.setColour(juce::Label::textColourId, theme::colours::muted);
    m_subtitle.setText("Waiting for a device", juce::dontSendNotification);
    m_subtitle.setColour(juce::Label::textColourId, theme::colours::faint);
    m_play->setGlyph(IconButton::Glyph::play);
    for (auto* b : {m_prev.get(), m_play.get(), m_next.get()})
        b->setEnabled(false);
    m_volume.setEnabled(false);
    m_solo.setEnabled(false);
    m_mute.setEnabled(false);
    m_meter.setDimmed(false);
    m_meter.setLevels(0.0f, 0.0f, 0.0f);
    applyControlVisibility();
    setAlpha(0.72f);
    repaint();
}

void SourceRow::update(const core::session::SourceSnapshot& snap, bool audible)
{
    m_empty = false;
    m_sourceId = snap.sourceId;
    m_live = snap.live;
    m_playing = snap.state == core::dsp::StreamState::playing || snap.state == core::dsp::StreamState::fadingOut;

    const juce::String device = snap.clientName.empty() ? prettyPeer(snap.peer) : juce::String(snap.clientName);
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
    m_title.setColour(juce::Label::textColourId, theme::colours::text);
    m_subtitle.setColour(juce::Label::textColourId, theme::colours::subtitle);
    m_title.setText(title, juce::dontSendNotification);
    m_subtitle.setText(subtitle, juce::dontSendNotification);

    m_stateText = stateText(snap.state, snap.live);
    m_stateColour = !snap.live    ? theme::colours::faint
                    : m_playing   ? theme::colours::playing
                                  : theme::colours::muted;

    m_play->setGlyph(m_playing ? IconButton::Glyph::pause : IconButton::Glyph::play);

    if (snap.artwork.size() != m_artworkSize) {
        m_artworkSize = snap.artwork.size();
        m_artwork = snap.artwork.empty()
                        ? juce::Image()
                        : juce::ImageFileFormat::loadFrom(snap.artwork.data(), snap.artwork.size());
    }

    m_meter.setDimmed(!audible);
    m_meter.setLevels(snap.peakL, snap.peakR, snap.rms);

    if (std::abs(m_volume.getValue() - snap.userGainDb) > 0.05 && !m_volume.isMouseButtonDown())
        m_volume.setValue(snap.userGainDb, juce::dontSendNotification);
    m_mute.setToggleState(snap.mute, juce::dontSendNotification);
    m_solo.setToggleState(snap.solo, juce::dontSendNotification);

    // Transport stays available whenever the sender is remote-controllable
    // (DACP-ID + Active-Remote), even while paused and not live, so play can
    // resume it. The local gain is always ours to set.
    const bool hasRemote = !snap.dacpId.empty() && !snap.activeRemote.empty();
    for (auto* b : {m_prev.get(), m_play.get(), m_next.get()})
        b->setEnabled(hasRemote);
    m_volume.setEnabled(true);
    m_solo.setEnabled(true);
    m_mute.setEnabled(true);

    m_pending = snap.pending;
    if (m_pending) {
        const juce::String device = snap.clientName.empty() ? prettyPeer(snap.peer) : juce::String(snap.clientName);
        m_title.setColour(juce::Label::textColourId, theme::colours::text);
        m_title.setText(device, juce::dontSendNotification);
        m_subtitle.setColour(juce::Label::textColourId, theme::colours::muted);
        m_subtitle.setText("Wants to connect", juce::dontSendNotification);
    }
    applyControlVisibility();
    setAlpha(m_pending || snap.live || hasRemote ? 1.0f : 0.68f);
    repaint();
}

void SourceRow::resized()
{
    auto r = getLocalBounds().reduced(14);
    r.removeFromLeft((getHeight() - 28) + 13); // full-height art well + gap (painted)

    auto sm = r.removeFromRight(30);
    m_solo.setBounds(sm.removeFromTop(26));
    sm.removeFromTop(4);
    m_mute.setBounds(sm.removeFromTop(26));
    r.removeFromRight(13);

    // The strip stacks header / meter / controls with even gaps, like a
    // console channel strip. The state tag sits on the title line only, so
    // reserve its space on the title row and let the subtitle use the full
    // width — otherwise long captions truncate with an ellipsis.
    auto top = r.removeFromTop(36);
    auto titleRow = top.removeFromTop(20);
    titleRow.removeFromRight(92); // reserved for the state tag
    m_title.setBounds(titleRow);
    m_subtitle.setBounds(top.removeFromTop(16));

    // Controls row along the bottom of the mid column; the meter fills the
    // band between it and the header with equal breathing room on both sides.
    auto ctrls = r.removeFromBottom(34);
    m_meter.setBounds(r.reduced(0, 5));

    // Allow / Decline share the full controls row (shown only when pending).
    {
        auto pr = ctrls;
        const int bw = (pr.getWidth() - 12) / 2;
        m_decline.setBounds(pr.removeFromLeft(bw).withSizeKeepingCentre(bw, 30));
        m_allow.setBounds(pr.removeFromRight(bw).withSizeKeepingCentre(bw, 30));
    }

    m_prev->setBounds(ctrls.removeFromLeft(32).withSizeKeepingCentre(32, 32));
    ctrls.removeFromLeft(10);
    m_play->setBounds(ctrls.removeFromLeft(38).withSizeKeepingCentre(38, 38));
    ctrls.removeFromLeft(10);
    m_next->setBounds(ctrls.removeFromLeft(32).withSizeKeepingCentre(32, 32));
    ctrls.removeFromLeft(14);
    ctrls.removeFromLeft(18); // speakerLow (painted)
    ctrls.removeFromRight(18); // speakerHigh (painted)
    m_volume.setBounds(ctrls.withSizeKeepingCentre(ctrls.getWidth(), 20));
}

void SourceRow::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(3.0f);
    theme::drawRaised(g, b, 16.0f);

    // Artwork well: a square spanning the slot's full inner height.
    const float side = static_cast<float>(getHeight()) - 28.0f;
    auto art = juce::Rectangle<float>(14.0f, 14.0f, side, side);
    theme::drawInset(g, art, 16.0f, m_empty ? theme::colours::inset : theme::colours::slot);
    if (m_artwork.isValid()) {
        juce::Path clip;
        clip.addRoundedRectangle(art.reduced(1.5f), 14.5f);
        g.saveState();
        g.reduceClipRegion(clip);
        // drawImage uses the context's current opacity; earlier shadow fills
        // leave it below 1, so reset to full white before drawing the artwork.
        g.setColour(juce::Colours::white);
        g.drawImage(m_artwork, art.reduced(1.5f),
                    juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        g.restoreState();
    } else if (m_empty) {
        const float glyph = side * 0.30f;
        theme::icons::stack(g, juce::Rectangle<float>(glyph, glyph).withCentre(art.getCentre()),
                            theme::colours::faint);
    } else {
        const float glyph = side * 0.33f;
        theme::icons::reel(g, juce::Rectangle<float>(glyph, glyph).withCentre(art.getCentre()),
                           juce::Colour(0xff5c6672), 1.5f);
    }

    // State tag (dot + coloured text, no background), top-right of the mid area.
    const juce::String tagText = m_pending ? juce::String("Pending") : m_stateText;
    const juce::Colour tagColour = m_pending ? theme::colours::accent : m_stateColour;
    if ((m_pending || !m_empty) && tagText.isNotEmpty()) {
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        const float tw = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), tagText);
        const float right = static_cast<float>(getWidth()) - 14.0f - 30.0f - 13.0f;
        auto tag = juce::Rectangle<float>(right - tw, 20.0f, tw, 14.0f);
        g.setColour(tagColour);
        g.fillEllipse(tag.getX() - 13.0f, tag.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.drawText(tagText, tag, juce::Justification::centredLeft);
    }

    // Speaker glyphs flanking the volume slider (hidden while pending, when the
    // volume fader is replaced by Allow/Decline).
    if (m_pending) {
        return;
    }
    const auto vb = m_volume.getBounds().toFloat();
    const juce::Colour spk = m_empty || !m_live ? theme::colours::dim : theme::colours::faint;
    theme::icons::speakerLow(g, juce::Rectangle<float>(15, 15).withCentre({vb.getX() - 10.0f, vb.getCentreY()}), spk);
    theme::icons::speakerHigh(g, juce::Rectangle<float>(15, 15).withCentre({vb.getRight() + 10.0f, vb.getCentreY()}),
                              spk);
}

} // namespace tapein::app
