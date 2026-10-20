// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "DonateModal.h"

#include "Theme.h"

#include <BinaryData.h>

namespace tapein::app {

namespace {
// GitHub Sponsors for the maintainer; falls back to the profile page until
// Sponsors is enabled on the account.
constexpr const char* kDonateUrl = "https://github.com/sponsors/GabrielTha";
} // namespace

// A raised amber pill with dark text; the one loud control in the app.
class DonateModal::AccentButton final : public juce::Button {
public:
    explicit AccentButton(const juce::String& text) : juce::Button(text) {}

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced(4.0f);
        auto fill = theme::colours::accent.brighter(highlighted ? 0.18f : 0.08f);
        // Soft amber halo so the button glows against the dark card.
        g.setColour(theme::colours::accent.withAlpha(0.30f));
        g.fillRoundedRectangle(b.expanded(4.0f), b.getHeight() * 0.5f + 4.0f);
        if (down) {
            theme::drawInset(g, b, b.getHeight() * 0.5f, fill);
        } else {
            theme::drawRaised(g, b, b.getHeight() * 0.5f, fill);
        }
        g.setColour(juce::Colour(0xff1a1208));
        g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        g.drawText(getButtonText(), b, juce::Justification::centred);
    }
};

DonateModal::DonateModal()
    : m_gabriel(juce::String(juce::CharPointer_UTF8("Gabriel Th\xc3\xa1 \xf0\x9f\x87\xa7\xf0\x9f\x87\xb7")),
                juce::URL("https://www.instagram.com/gabrielzuuk")),
      m_adam(juce::String(juce::CharPointer_UTF8("Adam Kershen \xf0\x9f\x87\xa8\xf0\x9f\x87\xa6")),
             juce::URL("https://www.instagram.com/djadamk")),
      m_link("github.com/sponsors/GabrielTha", juce::URL(kDonateUrl))
{
    setInterceptsMouseClicks(true, true);

    m_avatar = juce::ImageCache::getFromMemory(BinaryData::gabriel_avatar_png, BinaryData::gabriel_avatar_pngSize);

    for (auto* name : {&m_gabriel, &m_adam}) {
        name->setFont(juce::FontOptions(15.0f, juce::Font::bold), false, juce::Justification::centred);
        name->setColour(juce::HyperlinkButton::textColourId, theme::colours::accent);
        addAndMakeVisible(*name);
    }
    m_gabriel.setTooltip("@gabrielzuuk on Instagram");
    m_adam.setTooltip("@djadamk on Instagram");

    m_donate = std::make_unique<AccentButton>("DONATE ANY AMOUNT");
    m_donate->onClick = [] { juce::URL(kDonateUrl).launchInDefaultBrowser(); };
    addAndMakeVisible(*m_donate);

    m_link.setFont(juce::FontOptions(12.0f), false, juce::Justification::centred);
    m_link.setColour(juce::HyperlinkButton::textColourId, theme::colours::dim);
    addAndMakeVisible(m_link);
}

DonateModal::~DonateModal() = default;

juce::Rectangle<int> DonateModal::cardBounds() const
{
    return juce::Rectangle<int>(0, 0, getWidth() - 112, 384).withCentre(getLocalBounds().getCentre());
}

void DonateModal::mouseDown(const juce::MouseEvent& e)
{
    // A click on the scrim (outside the card) closes.
    if (!cardBounds().contains(e.getPosition()) && onClose) {
        onClose();
    }
}

void DonateModal::resized()
{
    auto card = cardBounds();
    auto a = card.reduced(28, 26);
    a.removeFromTop(64 + 14 + 26 + 10); // avatar + title (painted)
    a.removeFromTop(20);                // "made by" line (painted)

    // The two names, centred side by side around a painted dot.
    const auto names = a.removeFromTop(26);
    const juce::Font nameFont(juce::FontOptions(15.0f, juce::Font::bold));
    const int w1 = static_cast<int>(juce::GlyphArrangement::getStringWidth(nameFont, m_gabriel.getButtonText())) + 12;
    const int w2 = static_cast<int>(juce::GlyphArrangement::getStringWidth(nameFont, m_adam.getButtonText())) + 12;
    const int sep = 16;
    int x = names.getCentreX() - (w1 + sep + w2) / 2;
    m_gabriel.setBounds(x, names.getY(), w1, names.getHeight());
    m_adam.setBounds(x + w1 + sep, names.getY(), w2, names.getHeight());

    a.removeFromTop(20 + 6 + 40 + 12); // follow + donation lines (painted)
    m_donate->setBounds(a.removeFromTop(54).withSizeKeepingCentre(252, 54));
    a.removeFromTop(6);
    m_link.setBounds(a.removeFromTop(20));
}

void DonateModal::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::scrim);
    const auto card = cardBounds().toFloat();
    theme::drawRaised(g, card, 20.0f, theme::colours::slot);

    auto a = cardBounds().reduced(28, 26);

    // Avatar, clipped to a circle with a soft amber ring.
    const auto avatarBox = a.removeFromTop(64).withSizeKeepingCentre(64, 64).toFloat();
    if (m_avatar.isValid()) {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path circle;
        circle.addEllipse(avatarBox);
        g.reduceClipRegion(circle);
        g.setColour(juce::Colours::white); // full opacity for the image draw
        g.drawImage(m_avatar, avatarBox, juce::RectanglePlacement::fillDestination);
    }
    g.setColour(theme::colours::accent.withAlpha(0.8f));
    g.drawEllipse(avatarBox.reduced(0.5f), 1.5f);

    a.removeFromTop(14);
    g.setColour(theme::colours::accent);
    g.setFont(theme::boldonseFont(14.0f));
    g.drawText("SUPPORT TAPE IN", a.removeFromTop(26), juce::Justification::centred);

    a.removeFromTop(10);
    g.setColour(juce::Colour(0xffd9dee4));
    g.setFont(juce::FontOptions(15.0f));
    g.drawText("Tape In is free, open-source software made by", a.removeFromTop(20), juce::Justification::centred);

    // The names row holds the two Instagram links; paint the dot between them.
    const auto namesRow = a.removeFromTop(26);
    g.setColour(theme::colours::faint);
    g.drawText(juce::String(juce::CharPointer_UTF8("\xc2\xb7")), namesRow, juce::Justification::centred);

    g.setColour(juce::Colour(0xffd9dee4));
    g.drawText("Consider a follow to support the work.", a.removeFromTop(20), juce::Justification::centred);
    a.removeFromTop(6);
    g.setColour(juce::Colour(0xffb9c0c8));
    g.drawFittedText("If it is useful to you, a donation of any\namount helps keep it going.",
                     a.removeFromTop(40), juce::Justification::centredTop, 2);
}

} // namespace tapein::app
