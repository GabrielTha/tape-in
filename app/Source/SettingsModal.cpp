// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "SettingsModal.h"

#include "Theme.h"

namespace tapein::app {

// A 28x28 inset well that shows an amber check when on.
class SettingsModal::CheckBox final : public juce::Button {
public:
    CheckBox() : juce::Button({}) { setClickingTogglesState(true); }
    void paintButton(juce::Graphics& g, bool highlighted, bool /*down*/) override
    {
        auto b = getLocalBounds().toFloat();
        theme::drawInset(g, b, 9.0f, theme::colours::inset);
        if (getToggleState()) {
            theme::icons::check(g, juce::Rectangle<float>(16, 16).withCentre(b.getCentre()), theme::colours::accent,
                                2.4f);
        } else if (highlighted) {
            g.setColour(theme::colours::faint);
            g.drawRoundedRectangle(b.reduced(0.5f), 9.0f, 1.0f);
        }
    }
};

// A ".ddl" dropdown: raised, left-aligned text + chevron. Inert when it has
// no items (the Output 1/2 routing placeholders).
class SettingsModal::Dropdown final : public juce::Button {
public:
    Dropdown() : juce::Button({}) {}
    void setText(const juce::String& t)
    {
        m_text = t;
        repaint();
    }
    juce::String text() const { return m_text; }
    bool inert = false;
    void paintButton(juce::Graphics& g, bool /*highlighted*/, bool /*down*/) override
    {
        auto b = getLocalBounds().toFloat();
        theme::drawRaised(g, b.reduced(1.0f), 10.0f, theme::colours::background);
        g.setColour(inert ? theme::colours::faint : juce::Colour(0xffd3d7dd));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(m_text, b.withTrimmedLeft(13.0f).withTrimmedRight(30.0f), juce::Justification::centredLeft);
        theme::icons::chevron(g, juce::Rectangle<float>(13, 13).withCentre({b.getRight() - 13.0f, b.getCentreY()}),
                              theme::colours::faint, false);
    }

private:
    juce::String m_text;
};

SettingsModal::SettingsModal()
{
    setInterceptsMouseClicks(true, true);

    const auto styleField = [](juce::TextEditor& e) {
        e.setFont(juce::FontOptions(13.0f));
        e.setColour(juce::TextEditor::backgroundColourId, theme::colours::inset);
        e.setColour(juce::TextEditor::textColourId, theme::colours::text);
        e.setColour(juce::TextEditor::outlineColourId, juce::Colour(0x1affffff));
        e.setColour(juce::TextEditor::focusedOutlineColourId, theme::colours::accent.withAlpha(0.75f));
        e.setColour(juce::TextEditor::highlightColourId, theme::colours::accent.withAlpha(0.3f));
        e.setColour(juce::CaretComponent::caretColourId, theme::colours::accent);
    };

    styleField(m_name);
    addAndMakeVisible(m_name);

    m_restrict = std::make_unique<CheckBox>();
    m_restrict->onClick = [this] { relayout(); };
    addAndMakeVisible(*m_restrict);

    styleField(m_password);
    m_password.setPasswordCharacter(static_cast<juce::juce_wchar>(0x2022));
    m_password.setTextToShowWhenEmpty("Password", theme::colours::faint);
    addChildComponent(m_password);

    m_exclusive = std::make_unique<CheckBox>();
    addAndMakeVisible(*m_exclusive);

    m_audio = std::make_unique<Dropdown>();
    m_audio->onClick = [this] {
        if (m_audioDevices.isEmpty())
            return;
        juce::PopupMenu menu;
        for (int i = 0; i < m_audioDevices.size(); ++i)
            menu.addItem(i + 1, m_audioDevices[i], true, m_audioDevices[i] == m_audio->text());
        juce::Component::SafePointer<SettingsModal> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_audio.get()).withStandardItemHeight(32),
                           [safe](int r) {
                               if (safe == nullptr || r <= 0)
                                   return;
                               safe->m_audio->setText(safe->m_audioDevices[r - 1]);
                           });
    };
    addAndMakeVisible(*m_audio);

    m_out1 = std::make_unique<Dropdown>();
    m_out1->setText("Output 1");
    m_out1->inert = true;
    m_out1->setTooltip("Channel routing is coming in a later version");
    addAndMakeVisible(*m_out1);

    m_out2 = std::make_unique<Dropdown>();
    m_out2->setText("Output 2");
    m_out2->inert = true;
    m_out2->setTooltip("Channel routing is coming in a later version");
    addAndMakeVisible(*m_out2);

    m_save.setColour(juce::TextButton::buttonColourId, theme::colours::accent);
    m_save.setColour(juce::TextButton::textColourOffId, theme::colours::background);
    m_save.onClick = [this] {
        if (onApply)
            onApply(gather());
        if (onClose)
            onClose();
    };
    addAndMakeVisible(m_save);
}

SettingsModal::~SettingsModal() = default;

void SettingsModal::setValues(const Values& v)
{
    m_name.setText(v.name, juce::dontSendNotification);
    m_restrict->setToggleState(v.requirePassword, juce::dontSendNotification);
    m_password.setText(v.password, juce::dontSendNotification);
    m_exclusive->setToggleState(v.exclusiveSolo, juce::dontSendNotification);
    m_audio->setText(v.audioDevice);
    relayout();
}

void SettingsModal::setAudioDevices(const juce::StringArray& names) { m_audioDevices = names; }

SettingsModal::Values SettingsModal::gather() const
{
    Values v;
    v.name = m_name.getText().trim();
    v.requirePassword = m_restrict->getToggleState();
    v.password = m_password.getText();
    v.exclusiveSolo = m_exclusive->getToggleState();
    v.audioDevice = m_audio->text();
    return v;
}

juce::Rectangle<int> SettingsModal::cardBounds() const
{
    const bool pw = m_restrict->getToggleState();
    const int h = pw ? 460 : 404;
    return juce::Rectangle<int>(28, 56, getWidth() - 56, h);
}

void SettingsModal::mouseDown(const juce::MouseEvent& e)
{
    // A click on the scrim (outside the card) cancels.
    if (!cardBounds().contains(e.getPosition()) && onClose)
        onClose();
}

void SettingsModal::relayout()
{
    m_password.setVisible(m_restrict->getToggleState());
    resized();
    repaint();
}

void SettingsModal::resized()
{
    auto card = cardBounds();
    auto a = card.reduced(26, 24);
    a.removeFromTop(34 + 12); // "SETTINGS" title (painted)

    const int labelW = 120;
    const int gap = 16;
    const int rowGap = 16;
    auto row = [&](int h) {
        auto r = a.removeFromTop(h);
        a.removeFromTop(rowGap);
        return r.withTrimmedLeft(labelW + gap);
    };

    m_name.setBounds(row(40));
    m_restrict->setBounds(row(40).removeFromLeft(28).withSizeKeepingCentre(28, 28));
    if (m_restrict->getToggleState())
        m_password.setBounds(row(40));
    m_exclusive->setBounds(row(40).removeFromLeft(28).withSizeKeepingCentre(28, 28));
    m_audio->setBounds(row(40));
    auto outputs = row(40);
    const int half = (outputs.getWidth() - 12) / 2;
    m_out1->setBounds(outputs.removeFromLeft(half));
    m_out2->setBounds(outputs.removeFromRight(half));

    m_save.setBounds(juce::Rectangle<int>(0, 0, 150, 46).withCentre({card.getCentreX(), card.getBottom() - 24 - 23}));
}

void SettingsModal::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::scrim);
    auto card = cardBounds().toFloat();
    theme::drawRaised(g, card, 20.0f, theme::colours::slot);

    // Title.
    g.setColour(theme::colours::accent);
    g.setFont(theme::boldonseFont(12.0f));
    g.drawText("SETTINGS", card.reduced(26, 24).removeFromTop(20), juce::Justification::topLeft);

    // Right-aligned labels in the 120px column.
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xffaeb4bc));
    auto labelFor = [&](juce::Component& c, const juce::String& text) {
        auto b = c.getBounds();
        g.drawText(text, juce::Rectangle<int>(card.getX() + 26, b.getY(), 120, b.getHeight()),
                   juce::Justification::centredRight);
    };
    labelFor(m_name, "Network name");
    labelFor(*m_restrict, "Restrict access");
    if (m_password.isVisible())
        labelFor(m_password, "Password");
    labelFor(*m_exclusive, "Exclusive solo");
    labelFor(*m_audio, "Audio device");
    labelFor(*m_out1, "Output");
}

} // namespace tapein::app
