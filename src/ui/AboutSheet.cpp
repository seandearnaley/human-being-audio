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

juce::TextLayout storyLayout()
{
    juce::AttributedString text;
    text.setLineSpacing (bodyHeight * 0.25f);
    text.append (AboutSheet::story().joinIntoString ("\n\n"), uiFont (bodyHeight), palette::ink);

    juce::TextLayout layout;
    layout.createLayout (text, static_cast<float> (textWidth));
    return layout;
}
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
        g.drawRoundedRectangle (getLocalBounds().toFloat().expanded (3.0f), 4.0f, 1.5f);
}

AboutSheet::AboutSheet()
    : fade (juce::ValueAnimatorBuilder {}
                .withDurationMs (160.0)
                .withEasing (juce::Easings::createEaseOut())
                .withValueChangedCallback ([this] (float progress) { setAlpha (closing ? 1.0f - progress : progress); })
                .withOnCompleteCallback ([this]
                {
                    if (closing)
                        setVisible (false);
                })
                .build()),
      frames (this, [this]
      {
          if (! isVisible())
              return;

          snip += 0.012f;
          repaint (scissorsArea().toNearestInt());
      })
{
    setComponentID ("aboutSheet");
    setTitle ("About Livecut Redux");
    setDescription (story().joinIntoString (" "));
    setWantsKeyboardFocus (true);

    closeButton.setComponentID ("closeAbout");
    closeButton.setTitle ("Close");
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);

    source.setButtonText ("github.com/seandearnaley/livecut");
    source.setURL (juce::URL ("https://github.com/seandearnaley/livecut"));
    source.setTitle ("Source code on GitHub");
    source.setFont (uiFont (bodyHeight, weight::medium), false, juce::Justification::centredLeft);
    source.setColour (juce::HyperlinkButton::textColourId, palette::ink);
    addAndMakeVisible (source);

    animations.addAnimator (fade);
}

juce::StringArray AboutSheet::story()
{
    return {
        "Livecut was a live breakbeat cutter by Remy Muller, mdsp at Smartelectronix, written in 2003 and 2004. "
        "It chops the audio passing through it into phrases of cuts and repeats, using the algorithms of "
        "Nick Collins' BBCut library.",

        "It was never updated beyond 32-bit, so the hosts and computers people use now can't load it. "
        "Livecut Redux is a ground-up rewrite that can: a 64-bit VST3, Audio Unit and standalone app for "
        "Apple Silicon, Intel Macs and Windows.",

        "The original's three cut procedures, CutProc11, WarpCut and SQPusher, are all here, ported from its "
        "source and checked against it. The pad and its eight programs are new.",

        "Made by Sean Dearnaley and published by Human Being Music. Free software under the GNU General Public "
        "License, version 3 or later.",
    };
}

void AboutSheet::open()
{
    closing = false;
    setVisible (true);
    toFront (true);

    // Off screen there is no display refresh to drive the fade.
    if (isShowing())
    {
        setAlpha (0.0f);
        fade.start();
    }
    else
    {
        setAlpha (1.0f);
    }
}

void AboutSheet::close()
{
    closing = true;

    if (isShowing())
        fade.start();
    else
        setVisible (false);
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

    storyLayout().draw (g, { left, static_cast<float> (storyTop), static_cast<float> (textWidth), static_cast<float> (getHeight() - storyTop) });
}

void AboutSheet::resized()
{
    closeButton.setBounds (getWidth() - 20 - 72, 20, 72, 30);
    source.setBounds (margin, storyTop + juce::roundToInt (std::ceil (storyLayout().getHeight())) + 14, 280, 26);
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
