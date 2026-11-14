// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/FrameClock.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace livecut::ui
{

// The plug-in's name at the top left of the editor. Pressing it opens the About
// sheet. As each block of cuts starts it makes its own cut again: CUT drops
// under the blade and springs back.
class Wordmark final : public juce::Button
{
public:
    Wordmark();

    // Makes the cut. The editor calls it as each block starts.
    void cut();
    bool isCutting() const { return dropped > 0.0f; }

    // Moves the spring on by this many seconds. The display's refresh calls it.
    void animate (double seconds);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    float dropped = 0.0f; // 1 as the blade falls, easing back to 0 as CUT springs up
    FrameClock clock;
    juce::VBlankAttachment frames;
};

// A sheet over the whole editor that says what the original Livecut
// was, why this rewrite exists and who made each, beside a pair of scissors
// that snip slowly at a strip of tape. Close, Escape or a click on the sheet
// puts it away. While it is open the keyboard stays inside it.
class AboutSheet final : public juce::Component
{
public:
    AboutSheet();

    // Fades the sheet in and gives the keyboard to its Close button.
    void open();

    // Fades it out. Once it has gone, onClosed is called.
    void close();
    std::function<void()> onClosed;

    // Moves the fade and the scissors on by this many seconds. The display's refresh calls it.
    void animate (double seconds);

    // What the sheet says, paragraph by paragraph.
    static juce::StringArray story();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // The link to the source, with a ring when it has the keyboard.
    struct Link final : juce::HyperlinkButton
    {
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    juce::Rectangle<float> scissorsArea() const;

    juce::TextButton closeButton { "Close" };
    Link source;
    juce::TextLayout text;
    float shown = 0.0f; // how far the sheet has faded in
    float snip = 0.0f;  // the scissors' slow open and shut, as a phase
    bool closing = false;

    FrameClock clock;
    juce::VBlankAttachment frames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutSheet)
};

} // namespace livecut::ui
