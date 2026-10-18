// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "TrayIcon.h"

namespace tapein::app {

TrayIcon::TrayIcon(core::platform::ILoginItem& loginItem) : m_loginItem(loginItem)
{
    setIconImage(makeIcon(), makeIcon());
    setIconTooltip("Tape In");
}

juce::Image TrayIcon::makeIcon()
{
    // A small cassette outline, drawn as a black template so macOS tints it
    // for the light or dark menu bar.
    juce::Image img(juce::Image::ARGB, 44, 44, true);
    juce::Graphics g(img);
    g.setColour(juce::Colours::black);
    auto body = juce::Rectangle<float>(4, 11, 36, 22);
    g.drawRoundedRectangle(body, 3.0f, 2.2f);
    g.drawEllipse(13.0f, 18.0f, 7.0f, 7.0f, 2.0f);
    g.drawEllipse(24.0f, 18.0f, 7.0f, 7.0f, 2.0f);
    g.fillRect(juce::Rectangle<float>(13.0f, 29.0f, 18.0f, 3.0f));
    return img;
}

void TrayIcon::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) {
        showMenu();
    } else if (onToggleWindow) {
        onToggleWindow();
    }
}

void TrayIcon::showMenu()
{
    using Status = core::platform::ILoginItem::Status;
    juce::PopupMenu menu;
    menu.addItem(1, "Show Tape In");
    const bool enabled = m_loginItem.status() == Status::enabled;
    menu.addItem(2, "Start at login", true, enabled);
    if (isIgnoreSenderVolume) {
        menu.addItem(5, "Ignore sender volume", true, isIgnoreSenderVolume());
    }
    menu.addSeparator();
    menu.addItem(3, "About Tape In\xe2\x80\xa6");
    menu.addSeparator();
    menu.addItem(4, "Quit Tape In");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(nullptr), [this](int choice) {
        switch (choice) {
        case 1:
            if (onToggleWindow) {
                onToggleWindow();
            }
            break;
        case 2: {
            const bool want = m_loginItem.status() != core::platform::ILoginItem::Status::enabled;
            (void)m_loginItem.setEnabled(want);
            if (onStartAtLoginChanged) {
                onStartAtLoginChanged(want);
            }
            break;
        }
        case 5:
            if (onIgnoreSenderVolume && isIgnoreSenderVolume) {
                onIgnoreSenderVolume(!isIgnoreSenderVolume());
            }
            break;
        case 3:
            if (onAbout) {
                onAbout();
            }
            break;
        case 4:
            if (onQuit) {
                onQuit();
            }
            break;
        default:
            break;
        }
    });
}

} // namespace tapein::app
