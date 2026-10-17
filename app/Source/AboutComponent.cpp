// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "AboutComponent.h"

namespace tapein::app {

namespace {
constexpr const char* kSourceUrl = TAPEIN_SOURCE_URL;
} // namespace

AboutComponent::AboutComponent()
    : m_source("Source code", juce::URL(kSourceUrl)),
      m_notices("Third-party notices", juce::URL(juce::String(kSourceUrl) + "/blob/main/THIRD_PARTY.md"))
{
    m_title.setText("Tape In " JUCE_APPLICATION_VERSION_STRING, juce::dontSendNotification);
    m_title.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    m_title.setColour(juce::Label::textColourId, juce::Colours::white);
    m_title.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(m_title);

    m_body.setText("An open-source audio receiver that works with AirPlay.\n\n"
                   "Works with AirPlay. Not affiliated with Apple Inc. AirPlay is a "
                   "trademark of Apple Inc.\n\n"
                   "Free software under the GNU AGPL v3.",
                   juce::dontSendNotification);
    m_body.setFont(juce::FontOptions(13.0f));
    m_body.setColour(juce::Label::textColourId, juce::Colour(0xffc8ccd0));
    m_body.setJustificationType(juce::Justification::centredTop);
    addAndMakeVisible(m_body);

    for (auto* link : {&m_source, &m_notices}) {
        link->setColour(juce::HyperlinkButton::textColourId, juce::Colour(0xff58a6ff));
        link->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*link);
    }
}

void AboutComponent::resized()
{
    auto r = getLocalBounds().reduced(20);
    m_title.setBounds(r.removeFromTop(30));
    r.removeFromTop(8);
    m_body.setBounds(r.removeFromTop(140));
    r.removeFromTop(4);
    m_source.setBounds(r.removeFromTop(22));
    m_notices.setBounds(r.removeFromTop(22));
}

void AboutComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff121417));
}

void AboutComponent::show()
{
    juce::DialogWindow::LaunchOptions o;
    auto content = std::make_unique<AboutComponent>();
    content->setSize(360, 250);
    o.content.setOwned(content.release());
    o.dialogTitle = "About Tape In";
    o.dialogBackgroundColour = juce::Colour(0xff121417);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

} // namespace tapein::app
