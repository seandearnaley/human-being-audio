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
constexpr double frame = 1.0 / 30; // the scissors are redrawn this often
constexpr double springFrame = 1.0 / 60;
constexpr float springSeconds = 0.3f; // how long CUT takes to spring back
} // namespace

Wordmark::Wordmark()
    : juce::Button ("About Livecut Redux"),
      frames (this, [this] (double now)
      {
          // The clock keeps time while nothing moves, so a cut's first frame is as short as the rest.
          if (const auto step = clock.tick (now, springFrame); step.has_value() && isCutting())
              animate (*step);
      })
{
    setComponentID ("about");
    setTitle (getName());
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Wordmark::cut()
{
    dropped = 1.0f;
    repaint();
}

void Wordmark::animate (double seconds)
{
    dropped = juce::jmax (0.0f, dropped - static_cast<float> (seconds) / springSeconds);
    repaint();
}

void Wordmark::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (palette::ink.withAlpha (down ? 0.5f : highlighted ? 0.7f : 1.0f));
    drawWordmark (g, area.removeFromTop (23.0f), dropped * dropped);
    const auto font = uiFont (13.0f, weight::medium);
    g.setFont (font);
    g.drawText ("Redux", area.translated (-sideBearing (font, "Redux"), 0.0f), juce::Justification::centredLeft);

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


}

juce::StringArray AboutSheet::story()
{
    return {
        "Livecut was a live breakbeat cutter by Remy Muller, mdsp at Smartelectronix, written in 2003 and 2004. "
        "It chops the audio passing through it into phrases of cuts and repeats, using the algorithms of "
        "Nick Collins' BBCut library.",

        "It was only ever released as a 32-bit plug-in, so the hosts and computers people use now can't load it. "
        "Livecut Redux is a ground-up rewrite for them: a 64-bit VST3, Audio Unit and standalone app for "
        "macOS, Windows and Ubuntu.",

        "The original's three cut procedures, CutProc11, WarpCut and SQPusher, are all here, ported from its "
        "source with its bugs fixed against BBCut. The pad and its eight programs are new.",

        juce::String ("Made by Sean Dearnaley and published by ") + hba::product::publisher + ". Free software under the GNU General Public "
        "License, version 3 or later.",
    };
}

void AboutSheet::open()
{
    closing = false;
    StudioAbout::open();
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
    g.fillAll (palette::backdrop);
    StudioAbout::paint (g);
    drawScissors (g, scissorsArea(), 0.5f + 0.5f * std::sin (snip));

    const auto left = static_cast<float> (margin);
    g.setColour (palette::ink);
    drawWordmark (g, { left, 96.0f, static_cast<float> (textWidth), 76.0f });

    // Each size is set from its first letter's own edge, so they all share the wordmark's.
    const auto redux = uiFont (40.0f, weight::medium);
    g.setFont (redux);
    g.drawText ("Redux", juce::Rectangle<float> (left - sideBearing (redux, "Redux"), 176.0f, 200.0f, 44.0f), juce::Justification::centredLeft);
    g.setColour (palette::inkMuted);
    g.setFont (numberFont (bodyHeight));
    g.drawText (JucePlugin_VersionString,
                juce::Rectangle<float> (left + juce::GlyphArrangement::getStringWidth (redux, "Redux") + 12.0f, 186.0f, 200.0f, 30.0f),
                juce::Justification::centredLeft);

    text.draw (g, { left - sideBearing (uiFont (bodyHeight), "L"), static_cast<float> (storyTop), static_cast<float> (textWidth),
                    static_cast<float> (getHeight() - storyTop) });
}

void AboutSheet::mouseUp (const juce::MouseEvent&)
{
    close();
}

} // namespace livecut::ui
