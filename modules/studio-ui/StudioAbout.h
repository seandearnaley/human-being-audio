// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "studio/Product.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace hba {

// Both editors use this JUCE surface. The space below its header belongs to the product.
class StudioAbout : public juce::Component {
public:
    StudioAbout() {
        setComponentID("aboutSheet");
        setTitle(juce::String("About ") + product::name);
        setDescription(juce::String(product::publisher) + " / " + product::name + " " + product::version + " / " + product::license);
        setWantsKeyboardFocus(true);
        setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        source.setComponentID("aboutSource");
        source.setTitle(juce::String(product::name) + " source code on GitHub");
        source.setColour(juce::HyperlinkButton::textColourId, juce::Colours::white);
        closeButton.setComponentID("closeAbout");
        closeButton.setTitle("Close");
        closeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff34332e));
        closeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible(closeButton);
        addAndMakeVisible(source);
    }

    virtual void open() {
        setVisible(true);
        toFront(false);
        if (isShowing()) closeButton.grabKeyboardFocus();
    }
    virtual void close() {
        setVisible(false);
        if (onClosed) onClosed();
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key != juce::KeyPress::escapeKey) return false;
        close();
        return true;
    }
    void resized() override {
        auto row = getLocalBounds().removeFromTop(64).reduced(20, 16);
        closeButton.setBounds(row.removeFromRight(78));
        row.removeFromRight(12);
        source.setBounds(row.removeFromRight(100));
    }

    void paintOverChildren(juce::Graphics& g) override {
        // Only paint the left-hand masthead; the buttons own the right-hand side.
        auto header = getLocalBounds().removeFromTop(64);
        header.removeFromRight(218);
        header = header.reduced(24, 0);
        g.setColour(juce::Colour(0xffeabe72));
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText(product::publisher, header.removeFromTop(34), juce::Justification::bottomLeft);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(juce::String(product::name) + "  /  " + product::version + "  /  " + product::license,
                   header, juce::Justification::centredLeft);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff21211f));
        g.fillRect(getLocalBounds().removeFromTop(64));
    }
    std::function<void()> onClosed;

private:
    juce::TextButton closeButton { "Close" };
    juce::HyperlinkButton source { "Source code", juce::URL(product::source) };
};
}
