// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "DeviceButton.h"

#include "Theme.h"

namespace tapein::app {

DeviceButton::DeviceButton() : juce::Button({})
{
    onClick = [this] { showMenu(); };
}

void DeviceButton::setDevices(const juce::StringArray& names, const juce::String& current)
{
    m_names = names;
    m_current = current;
    repaint();
}

void DeviceButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat();
    if (m_open || down) {
        theme::drawInset(g, b, 11.0f, theme::colours::slot);
    } else {
        theme::drawRaised(g, b.reduced(1.0f), 10.0f, theme::colours::background);
    }
    const juce::Colour ink = m_open ? theme::colours::accent
                             : highlighted ? theme::colours::text
                                           : juce::Colour(0xffd3d7dd);
    const juce::Colour iconCol = m_open ? theme::colours::accent : juce::Colour(0xff8b929c);

    auto row = b.reduced(13.0f, 0.0f);
    theme::icons::speakerLow(g, juce::Rectangle<float>(15, 15).withCentre({row.getX() + 7.5f, row.getCentreY()}),
                             iconCol);
    const float chevBox = 13.0f;
    auto chev = juce::Rectangle<float>(chevBox, chevBox).withCentre({row.getRight() - 6.5f, row.getCentreY()});
    theme::icons::chevron(g, chev, m_open ? theme::colours::accent : theme::colours::faint, m_open);

    g.setColour(ink);
    g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    g.drawText(m_current.isEmpty() ? "Output device" : m_current,
               row.withTrimmedLeft(22.0f).withTrimmedRight(18.0f), juce::Justification::centred);
}

void DeviceButton::showMenu()
{
    if (m_names.isEmpty()) {
        return;
    }
    juce::PopupMenu menu;
    for (int i = 0; i < m_names.size(); ++i) {
        menu.addItem(i + 1, m_names[i], true, m_names[i] == m_current);
    }
    m_open = true;
    repaint();
    auto opts = juce::PopupMenu::Options()
                    .withTargetComponent(this)
                    .withMinimumWidth(juce::jmax(220, getWidth()))
                    .withStandardItemHeight(38);
    menu.showMenuAsync(opts, [this](int result) {
        m_open = false;
        repaint();
        if (result > 0 && result <= m_names.size() && onSelect) {
            onSelect(m_names[result - 1]);
        }
    });
}

} // namespace tapein::app
