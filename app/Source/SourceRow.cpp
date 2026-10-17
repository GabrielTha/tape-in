// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "SourceRow.h"

namespace tapein::app {

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
        return "Playing";
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
    m_name.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    m_name.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(m_name);

    m_state.setFont(juce::FontOptions(12.0f));
    m_state.setColour(juce::Label::textColourId, juce::Colour(0xff9aa0a6));
    addAndMakeVisible(m_state);

    addAndMakeVisible(m_meter);

    m_gain.setSliderStyle(juce::Slider::LinearHorizontal);
    m_gain.setRange(-60.0, 12.0, 0.1);
    m_gain.setValue(0.0, juce::dontSendNotification);
    m_gain.setDoubleClickReturnValue(true, 0.0);
    m_gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
    m_gain.setTextValueSuffix(" dB");
    m_gain.onValueChange = [this] {
        if (onGain) {
            onGain(m_sourceId, static_cast<float>(m_gain.getValue()));
        }
    };
    addAndMakeVisible(m_gain);

    m_mute.setClickingTogglesState(true);
    m_mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe4572e));
    m_mute.onClick = [this] {
        if (onMute) {
            onMute(m_sourceId, m_mute.getToggleState());
        }
    };
    addAndMakeVisible(m_mute);

    m_solo.setClickingTogglesState(true);
    m_solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xfff0c419));
    m_solo.onClick = [this] {
        if (onSolo) {
            onSolo(m_sourceId, m_solo.getToggleState());
        }
    };
    addAndMakeVisible(m_solo);
}

void SourceRow::update(const core::session::SourceSnapshot& snap)
{
    m_sourceId = snap.sourceId;
    m_live = snap.live;
    const juce::String name = snap.clientName.empty() ? juce::String(snap.peer) : juce::String(snap.clientName);
    m_name.setText(name, juce::dontSendNotification);
    juce::String state = stateText(snap.state, snap.live);
    if (snap.underruns > 0 && snap.live) {
        state += " \xc2\xb7 " + juce::String(snap.underruns) + " dropouts";
    }
    m_state.setText(state, juce::dontSendNotification);
    m_meter.setLevels(snap.live ? snap.peakL : 0.0f, snap.live ? snap.peakR : 0.0f);
    if (std::abs(m_gain.getValue() - snap.userGainDb) > 0.05 && !m_gain.isMouseButtonDown()) {
        m_gain.setValue(snap.userGainDb, juce::dontSendNotification);
    }
    m_mute.setToggleState(snap.mute, juce::dontSendNotification);
    m_solo.setToggleState(snap.solo, juce::dontSendNotification);
    setAlpha(snap.live ? 1.0f : 0.5f);
}

void SourceRow::resized()
{
    auto r = getLocalBounds().reduced(8, 6);
    auto top = r.removeFromTop(20);
    m_name.setBounds(top.removeFromLeft(top.getWidth() - 70));
    m_state.setBounds(top);
    r.removeFromTop(4);
    auto bottom = r;
    m_solo.setBounds(bottom.removeFromRight(28).reduced(1));
    m_mute.setBounds(bottom.removeFromRight(28).reduced(1));
    bottom.removeFromRight(6);
    m_meter.setBounds(bottom.removeFromLeft(90));
    bottom.removeFromLeft(8);
    m_gain.setBounds(bottom);
}

void SourceRow::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff1b1e23));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 6.0f);
}

} // namespace tapein::app
