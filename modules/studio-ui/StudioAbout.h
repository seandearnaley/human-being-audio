// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "studio/Product.h"
#include "HbaStudioArt.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace hba {

// Shared masthead and navigation. Each product paints the page below it.
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
        closeButton.setComponentID("closeAbout");
        closeButton.setTitle("Close");
        closeButton.setTooltip("Close About (Escape)");
        closeButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible(source);
        addAndMakeVisible(closeButton);
    }
    virtual void open() {
        setVisible(true);
        toFront(false);
        if (isShowing()) grabKeyboardFocus();
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
        auto row = getLocalBounds().removeFromTop(64).reduced(16, 10);
        closeButton.setBounds(row.removeFromRight(44));
        row.removeFromRight(8);
        source.setBounds(row.removeFromRight(90));
    }
    void paintOverChildren(juce::Graphics& g) override {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(mark, {23.0f, 15.0f, 34.0f, 34.0f});
        auto title = getLocalBounds().removeFromTop(64).withTrimmedLeft(68).withTrimmedRight(174);
        g.setColour(juce::Colour(0xfff3f4f6));
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText(product::publisher, title.removeFromTop(35), juce::Justification::bottomLeft);
        g.setColour(juce::Colour(0xffa8afb8));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(juce::String(product::name) + "  /  " + product::version + "  /  " + product::license,
                   title.removeFromTop(21), juce::Justification::centredLeft);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colours::white.withAlpha(0.16f));
        g.fillRect(20, 63, getWidth() - 40, 1);
    }
    std::function<void()> onClosed;

private:
    static void paintFocus(juce::Graphics& g, const juce::Button& button) {
        if (!button.hasKeyboardFocus(true)) return;
        g.setColour(juce::Colour(0xff86cce9));
        g.drawRoundedRectangle(button.getLocalBounds().toFloat().reduced(2), 4, 1);
    }
    struct Close : juce::Button {
        Close() : juce::Button("Close") {}
        void paintButton(juce::Graphics& g, bool hover, bool down) override {
            const auto c = getLocalBounds().toFloat().getCentre();
            g.setColour(hover || down ? juce::Colours::white : juce::Colour(0xffb6bec8));
            g.drawLine(c.x-6, c.y-6, c.x+6, c.y+6, 1.7f);
            g.drawLine(c.x+6, c.y-6, c.x-6, c.y+6, 1.7f);
            paintFocus(g, *this);
        }
    } closeButton;
    struct Source : juce::HyperlinkButton {
        Source() : HyperlinkButton("Source code", juce::URL(product::source)) {}
        void paintButton(juce::Graphics& g, bool hover, bool down) override {
            g.setColour(hover || down ? juce::Colours::white : juce::Colour(0xffbac3cf));
            g.setFont(juce::FontOptions(14.0f));
            auto area = getLocalBounds().toFloat().reduced(6, 0);
            auto arrow = area.removeFromRight(12).getCentre();
            g.drawText("Source code", area, juce::Justification::centredLeft);
            g.drawLine(arrow.x-3, arrow.y+3, arrow.x+3, arrow.y-3, 1.3f);
            g.drawLine(arrow.x-3, arrow.y-3, arrow.x+3, arrow.y-3, 1.3f);
            g.drawLine(arrow.x+3, arrow.y-3, arrow.x+3, arrow.y+3, 1.3f);
            paintFocus(g, *this);
        }
    } source;
    const juce::Image mark = juce::ImageCache::getFromMemory(HbaStudioArt::mark_png, HbaStudioArt::mark_pngSize);
};
}
