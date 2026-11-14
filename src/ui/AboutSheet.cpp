// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/AboutSheet.h"

#include "ui/PadArt.h"
#include "ui/Theme.h"

#include <cmath>

namespace livecut::ui
{

namespace
{
constexpr int margin = 56;
constexpr int textWidth = 420;
constexpr int storyTop = 256;
constexpr float bodyHeight = 16.0f;
constexpr float fadeSeconds = 0.16f;
constexpr int linkPadding = 6;     // room around the link's words for its focus ring
constexpr double frame = 1.0 / 30; // the scissors are redrawn this often
} // namespace

Wordmark::Wordmark() : juce::Button ("About Livecut Redux")
{
    setComponentID ("about");
    setTitle (getName());
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Wordmark::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (palette::ink.withAlpha (down ? 0.5f : highlighted ? 0.7f : 1.0f));
    drawWordmark (g, area.removeFromTop (26.0f));
    g.setFont (uiFont (14.0f, weight::medium));
    g.drawText ("Redux", area.translated (1.0f, 0.0f), juce::Justification::topLeft);

    if (hasKeyboardFocus (false))
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.5f);
}

void AboutSheet::Link::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    g.setColour (palette::ink.withAlpha (down ? 0.5f : highlighted ? 0.7f : 1.0f));
    g.setFont (uiFont (bodyHeight, weight::medium));
    g.drawText (getButtonText(), getLocalBounds().reduced (linkPadding, 0), juce::Justification::centredLeft);

    if (hasKeyboardFocus (false))
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.5f);
}

AboutSheet::AboutSheet()
    : frames (this, [this] (double now)
      {
          if (const auto elapsed = clock.tick (now, frame))
              animate (*elapsed);
      })
{
    setComponentID ("aboutSheet");
    setTitle ("About Livecut Redux");
    setDescription (story().joinIntoString (" "));
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setAlpha (0.0f);

    juce::AttributedString words;
    words.setLineSpacing (bodyHeight * 0.25f);
    words.append (story().joinIntoString ("\n\n"), uiFont (bodyHeight), palette::ink);
    text.createLayout (words, static_cast<float> (textWidth));

    closeButton.setComponentID ("closeAbout");
    closeButton.setTitle ("Close");
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);

    source.setButtonText ("github.com/seandearnaley/livecut");
    source.setURL (juce::URL ("https://github.com/seandearnaley/livecut"));
    source.setTitle ("Source code on GitHub");
    addAndMakeVisible (source);
}

juce::StringArray AboutSheet::story()
{
    return {
        "Livecut was a live breakbeat cutter by Remy Muller, mdsp at Smartelectronix, written in 2003 and 2004. "
        "It chops the audio passing through it into phrases of cuts and repeats, using the algorithms of "
        "Nick Collins' BBCut library.",

        "It was only ever released as a 32-bit plug-in, so the hosts and computers people use now can't load it. "
        "Livecut Redux is a ground-up rewrite for them: a 64-bit VST3, Audio Unit and standalone app for "
        "Apple Silicon, Intel Macs and Windows.",

        "The original's three cut procedures, CutProc11, WarpCut and SQPusher, are all here, ported from its "
        "source with its bugs fixed against BBCut. The pad and its eight programs are new.",

        "Made by Sean Dearnaley and published by Human Being Music. Free software under the GNU General Public "
        "License, version 3 or later.",
    };
}

void AboutSheet::open()
{
    closing = false;
    setVisible (true);
    toFront (false);
    closeButton.grabKeyboardFocus();
}

void AboutSheet::close()
{
    closing = true;
}

void AboutSheet::animate (double seconds)
{
    if (! isVisible())
        return;

    const auto elapsed = static_cast<float> (seconds);
    shown = juce::jlimit (0.0f, 1.0f, shown + (closing ? -elapsed : elapsed) / fadeSeconds);
    setAlpha (shown);

    if (closing && shown <= 0.0f)
    {
        setVisible (false);

        if (onClosed)
            onClosed();

        return;
    }

    snip += elapsed * 1.4f;
    repaint (scissorsArea().toNearestInt());
}

juce::Rectangle<float> AboutSheet::scissorsArea() const
{
    // The tape runs off the page's right edge.
    return juce::Rectangle<float> (360.0f, 360.0f).withCentre ({ static_cast<float> (getWidth()) - 180.0f, 372.0f });
}

void AboutSheet::paint (juce::Graphics& g)
{
    g.fillAll (palette::paper);
    drawScissors (g, scissorsArea(), 0.5f + 0.5f * std::sin (snip));

    const auto left = static_cast<float> (margin);
    g.setColour (palette::ink);
    drawWordmark (g, { left, 96.0f, static_cast<float> (textWidth), 76.0f });
    g.setFont (uiFont (40.0f, weight::medium));
    g.drawText ("Redux", juce::Rectangle<float> (left + 2.0f, 176.0f, 200.0f, 44.0f), juce::Justification::centredLeft);
    g.setColour (palette::inkMuted);
    g.setFont (numberFont (bodyHeight));
    g.drawText (JucePlugin_VersionString, juce::Rectangle<float> (left + 118.0f, 186.0f, 200.0f, 30.0f), juce::Justification::centredLeft);

    text.draw (g, { left, static_cast<float> (storyTop), static_cast<float> (textWidth), static_cast<float> (getHeight() - storyTop) });
}

void AboutSheet::resized()
{
    closeButton.setBounds (getWidth() - 20 - 72, 20, 72, 30);
    source.setBounds (margin - linkPadding, storyTop + juce::roundToInt (std::ceil (text.getHeight())) + 10, 260, 30);
}

void AboutSheet::mouseUp (const juce::MouseEvent&)
{
    close();
}

bool AboutSheet::keyPressed (const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey)
        return false;

    close();
    return true;
}

} // namespace livecut::ui
