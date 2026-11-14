// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_animation/juce_animation.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace livecut::ui
{

// The plug-in's name at the top left of the editor. Pressing it opens the About sheet.
class Wordmark final : public juce::Button
{
public:
    Wordmark();

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// A sheet of paper over the whole editor that says what the original Livecut
// was, why this rewrite exists and who made each, beside a pair of scissors
// that snip slowly at a strip of tape. Close, Escape or a click on the paper
// puts it away.
class AboutSheet final : public juce::Component
{
public:
    AboutSheet();

    void open();
    void close();

    // What the sheet says, paragraph by paragraph.
    static juce::StringArray story();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<float> scissorsArea() const;

    juce::TextButton closeButton { "Close" };
    juce::HyperlinkButton source;
    bool closing = false;
    float snip = 0.0f; // the scissors' slow open and shut, as a phase

    juce::VBlankAnimatorUpdater animations { this };
    juce::Animator fade;
    juce::VBlankAttachment frames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutSheet)
};

} // namespace livecut::ui
