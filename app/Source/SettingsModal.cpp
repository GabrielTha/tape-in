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
        g.setFont(juce::FontOptions(14.0f));
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
        e.setFont(juce::FontOptions(14.0f));
        e.setJustification(juce::Justification::centredLeft);
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

    m_ask = std::make_unique<CheckBox>();
    addAndMakeVisible(*m_ask);

    m_folder = std::make_unique<Dropdown>();
    m_folder->onClick = [this] {
        m_chooser = std::make_unique<juce::FileChooser>(
            "Choose a folder for samples", m_folderPath.isNotEmpty() ? juce::File(m_folderPath) : juce::File());
        m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                               [this](const juce::FileChooser& fc) {
                                   const juce::File r = fc.getResult();
                                   if (r != juce::File()) {
                                       m_folderPath = r.getFullPathName();
                                       m_folder->setText(r.getFileName());
                                   }
                               });
    };
    addAndMakeVisible(*m_folder);

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
                               if (safe->m_audioDevices[r - 1] != safe->m_audio->text()) {
                                   // A different device: its channel map is its
                                   // own, so fall back to the default 1/2 pair.
                                   safe->m_channelNames.clear();
                                   safe->m_outLeft = 0;
                                   safe->m_outRight = 1;
                                   safe->refreshChannelTexts();
                               }
                               safe->m_audio->setText(safe->m_audioDevices[r - 1]);
                           });
    };
    addAndMakeVisible(*m_audio);

    // Buffer-mode segmented control (same trough-and-pill look as the old
    // footer; the trough and the active pill are painted in paint()).
    for (int i = 0; i < 3; ++i) {
        auto& b = m_buffer[static_cast<size_t>(i)];
        b.setClickingTogglesState(true);
        b.setRadioGroupId(43);
        b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8b929c));
        b.setColour(juce::TextButton::textColourOnId, theme::colours::text);
        b.onClick = [this, i] {
            m_bufferIndex = i;
            repaint();
        };
        addAndMakeVisible(b);
    }

    m_out1 = std::make_unique<Dropdown>();
    m_out1->setTooltip("Device channel the mix left plays on");
    m_out1->onClick = [this] { showChannelMenu(true); };
    addAndMakeVisible(*m_out1);

    m_out2 = std::make_unique<Dropdown>();
    m_out2->setTooltip("Device channel the mix right plays on");
    m_out2->onClick = [this] { showChannelMenu(false); };
    addAndMakeVisible(*m_out2);
    refreshChannelTexts();

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

void SettingsModal::refreshChannelTexts()
{
    const auto nameFor = [this](int chan) {
        return chan >= 0 && chan < m_channelNames.size() ? m_channelNames[chan]
                                                         : "Output " + juce::String(chan + 1);
    };
    m_out1->setText(nameFor(m_outLeft));
    m_out2->setText(nameFor(m_outRight));
    m_out1->inert = m_channelNames.isEmpty();
    m_out2->inert = m_channelNames.isEmpty();
}

void SettingsModal::showChannelMenu(bool left)
{
    if (m_channelNames.isEmpty())
        return;
    const int current = left ? m_outLeft : m_outRight;
    juce::PopupMenu menu;
    for (int i = 0; i < m_channelNames.size(); ++i)
        menu.addItem(i + 1, m_channelNames[i], true, i == current);
    auto* target = left ? m_out1.get() : m_out2.get();
    juce::Component::SafePointer<SettingsModal> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target).withStandardItemHeight(32),
                       [safe, left](int r) {
                           if (safe == nullptr || r <= 0)
                               return;
                           (left ? safe->m_outLeft : safe->m_outRight) = r - 1;
                           safe->refreshChannelTexts();
                       });
}

void SettingsModal::setValues(const Values& v)
{
    m_name.setText(v.name, juce::dontSendNotification);
    m_restrict->setToggleState(v.requirePassword, juce::dontSendNotification);
    m_password.setText(v.password, juce::dontSendNotification);
    m_exclusive->setToggleState(v.exclusiveSolo, juce::dontSendNotification);
    m_ask->setToggleState(v.askBeforeAccept, juce::dontSendNotification);
    m_folderPath = v.samplesFolder;
    m_folder->setText(juce::File(v.samplesFolder).getFileName());
    m_audio->setText(v.audioDevice);
    m_bufferIndex = juce::jlimit(0, 2, v.bufferIndex);
    m_buffer[static_cast<size_t>(m_bufferIndex)].setToggleState(true, juce::dontSendNotification);
    m_outLeft = v.outLeft;
    m_outRight = v.outRight;
    refreshChannelTexts();
    relayout();
}

void SettingsModal::setAudioDevices(const juce::StringArray& names) { m_audioDevices = names; }

void SettingsModal::setOutputChannelNames(const juce::StringArray& names)
{
    m_channelNames = names;
    refreshChannelTexts();
}

SettingsModal::Values SettingsModal::gather() const
{
    Values v;
    v.name = m_name.getText().trim();
    v.requirePassword = m_restrict->getToggleState();
    v.password = m_password.getText();
    v.exclusiveSolo = m_exclusive->getToggleState();
    v.askBeforeAccept = m_ask->getToggleState();
    v.samplesFolder = m_folderPath;
    v.audioDevice = m_audio->text();
    v.bufferIndex = m_bufferIndex;
    v.outLeft = m_outLeft;
    v.outRight = m_outRight;
    return v;
}

juce::Rectangle<int> SettingsModal::cardBounds() const
{
    const bool pw = m_restrict->getToggleState();
    const int h = pw ? 658 : 602;
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
    m_ask->setBounds(row(40).removeFromLeft(28).withSizeKeepingCentre(28, 28));
    m_audio->setBounds(row(40));
    auto seg = row(40).removeFromLeft(210).reduced(4);
    const int sw = seg.getWidth() / 3;
    for (int i = 0; i < 3; ++i) {
        m_buffer[static_cast<size_t>(i)].setBounds(seg.removeFromLeft(i == 2 ? seg.getWidth() : sw).reduced(1));
    }
    m_folder->setBounds(row(40));
    auto outputs = row(40);
    const int half = (outputs.getWidth() - 12) / 2;
    m_out1->setBounds(outputs.removeFromLeft(half));
    m_out2->setBounds(outputs.removeFromRight(half));

    a.removeFromTop(8); // breathing room between the Output row and Save
    auto saveRow = a.removeFromTop(46);
    m_save.setBounds(juce::Rectangle<int>(0, 0, 150, 46).withCentre(saveRow.getCentre()));
}

void SettingsModal::paint(juce::Graphics& g)
{
    g.fillAll(theme::colours::scrim);
    auto card = cardBounds().toFloat();
    theme::drawRaised(g, card, 20.0f, theme::colours::slot);

    // Title.
    g.setColour(theme::colours::accent);
    g.setFont(theme::boldonseFont(15.0f));
    g.drawText("SETTINGS", card.reduced(26, 22).removeFromTop(26), juce::Justification::topLeft);

    // Right-aligned labels in the 120px column.
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xffc6ccd3));
    auto labelFor = [&](juce::Component& c, const juce::String& text) {
        auto b = c.getBounds();
        g.drawText(text, juce::Rectangle<int>(card.getX() + 26, b.getY(), 120, b.getHeight()),
                   juce::Justification::centredLeft);
    };
    labelFor(m_name, "Network name");
    labelFor(*m_restrict, "Restrict access");
    if (m_password.isVisible())
        labelFor(m_password, "Password");
    labelFor(*m_exclusive, "Exclusive solo");
    labelFor(*m_ask, "Ask to connect");
    labelFor(*m_audio, "Audio device");
    labelFor(m_buffer[0], "Buffer");
    labelFor(*m_folder, "Samples folder");
    labelFor(*m_out1, "Output");

    // Buffer trough and the raised pill under the active segment.
    const auto troughRect =
        m_buffer[0].getBounds().getUnion(m_buffer[2].getBounds()).toFloat().expanded(4.0f, 4.0f);
    theme::drawInset(g, troughRect, 11.0f, theme::colours::inset);
    if (m_bufferIndex >= 0 && m_bufferIndex < 3) {
        theme::drawRaised(g, m_buffer[static_cast<size_t>(m_bufferIndex)].getBounds().toFloat().reduced(1.0f), 8.0f,
                          theme::colours::slot);
    }
}

} // namespace tapein::app
