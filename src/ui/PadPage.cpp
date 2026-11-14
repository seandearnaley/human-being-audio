// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/PadPage.h"

#include "Parameters.h"
#include "ui/Theme.h"

#include <array>
#include <cmath>

namespace livecut::ui
{

namespace
{
constexpr int tabHeight = 36;
constexpr int tabGap = 4;
constexpr int restingTabInset = 6; // a tab whose program isn't playing sits this much lower
constexpr float tabRadius = 9.0f;
constexpr int rowHeight = 36;
constexpr int gap = 20;            // between the pad and the row beneath, as between everything on the surface
constexpr int controlGap = 24;     // between the controls in that row

// How much of its colour a tab's face takes on. A waiting tab stops at as much as white still reads on.
constexpr float hoveredFill = 0.12f, pressedFill = 0.3f, waitingFill = 0.2f, waitingLitFill = 0.5f;

const juce::String rightArrow = juce::CharPointer_UTF8 ("\xe2\x86\x92"); // →

// What the pad does in each program.
constexpr std::array<const char*, engine::padProgramCount> programTooltips {
    "Left to right: 8ths to 32nds with more repeats. Up: from the break as it is to a full re-edit.",
    "Left to right: more triplet bounce, with halftime bars at the far right. Up: ratchets climb faster and higher.",
    "Left to right: faster repeats that get crunchier as they speed up. Up: each repeat steps from falling to rising pitch.",
    "Left to right: the band slides from lows to highs. Up: it squeezes narrower and sings, breathing with every cut.",
    "Left to right: the filter opens from dark to bright. Up: it rings, and every roll ends on a tuned note that ducks under the hits.",
    "Left to right: stops from half a beat to four beats long. Up: from a stop every four bars to one on every beat, each spinning back up into the next hit.",
    "Left to right: loop the bar you're in, half a bar, or a beat. Up: the loop folds into shorter loops of its first hit toward the end of every phrase.",
    "Left to right: rolls that drag, slowing and falling, to rolls that rush, speeding up and rising into the bar line. Up: from a roll on the last beat to one that fills the bar.",
};

juce::RangedAudioParameter& parameter (juce::AudioProcessorValueTreeState& state, const char* id)
{
    auto* found = state.getParameter (id);
    jassert (found != nullptr);
    return *found;
}

juce::AudioParameterChoice& choice (juce::AudioProcessorValueTreeState& state, const char* id)
{
    auto* found = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id));
    jassert (found != nullptr);
    return *found;
}
} // namespace

TabColours tabColours (juce::Colour colour, TabState state, float lit)
{
    const auto face = [colour] (float filled) { return palette::well.interpolatedWith (colour, filled); };

    switch (state)
    {
        case TabState::resting: return { face (0.0f), colour };
        case TabState::hovered: return { face (hoveredFill), colour };
        case TabState::pressed: return { face (pressedFill), palette::ink };
        case TabState::waiting: return { face (waitingFill + (waitingLitFill - waitingFill) * lit), palette::ink };
        case TabState::playing: return { colour, palette::onColour };
    }

    jassertfalse;
    return {};
}

PadPage::ProgramButton::ProgramButton (const juce::String& name, juce::Colour tabColour)
    : juce::Button (name), colour (tabColour)
{
}

void PadPage::ProgramButton::setPending (bool next, float beat)
{
    // Lit on the beat, and fading until the next.
    const auto nowLit = next ? std::pow (1.0f - (beat - std::floor (beat)), 2.0f) : 0.0f;

    if (pending == next && juce::approximatelyEqual (lit, nowLit))
        return;

    pending = next;
    lit = nowLit;
    repaint();
}

void PadPage::ProgramButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto playing = getToggleState();
    const auto colours = tabColours (colour, playing ? TabState::playing : pending ? TabState::waiting : down ? TabState::pressed
                                                     : highlighted ? TabState::hovered : TabState::resting, lit);
    const auto resting = getLocalBounds().toFloat().withTrimmedTop (static_cast<float> (restingTabInset));
    const auto face = playing ? getLocalBounds().toFloat() : resting;

    juce::Path tab;
    tab.addRoundedRectangle (face.getX(), face.getY(), face.getWidth(), face.getHeight(), tabRadius, tabRadius, true, true, false, false);
    g.setColour (colours.face);
    g.fillPath (tab);

    // Every name sits on the same line, whether or not its tab stands taller.
    g.setColour (colours.name);
    g.setFont (uiFont (14.0f, playing ? weight::bold : weight::medium));
    g.drawText (getButtonText(), resting, juce::Justification::centred);

    if (hasKeyboardFocus (false))
    {
        g.setColour (playing ? palette::onColour : palette::ink);
        g.drawRoundedRectangle (face.reduced (2.0f), tabRadius - 2.0f, 1.5f);
    }
}

PadPage::PadPage (juce::AudioProcessorValueTreeState& s)
    : state (s),
      programParameter (choice (s, ids::padProgram)),
      pad (parameter (s, ids::padX), parameter (s, ids::padY), parameter (s, ids::padTouch)),
      programAttachment (programParameter, [this] (float value)
      {
          chosen = juce::jlimit (0, static_cast<int> (engine::padProgramCount) - 1, juce::roundToInt (value));
      }),
      hold (parameter (s, ids::padHold), "Hold"),
      straight (parameter (s, ids::padStraight), "Straight"),
      quantize (choice (s, ids::padQuantize)),
      mix (parameter (s, ids::wet), "Mix", SliderRow::Layout::oneLine)
{
    addAndMakeVisible (pad);

    programRow.setComponentID (ids::padProgram);
    programRow.setTitle (programParameter.getName (64));
    addAndMakeVisible (programRow);

    for (int i = 0; i < programParameter.choices.size(); ++i)
    {
        auto* button = programs.add (std::make_unique<ProgramButton> (programParameter.choices[i], programColour (i)));
        button->setComponentID ("program" + juce::String (i));
        button->setTitle (programParameter.choices[i]);
        button->setTooltip (programTooltips[static_cast<std::size_t> (i)]);
        button->onClick = [this, i] { programAttachment.setValueAsCompleteGesture (static_cast<float> (i)); };
        programRow.addAndMakeVisible (button);
    }

    const auto describeSwitch = [] (PowerSwitch& control, const juce::String& title, const juce::String& tooltip)
    {
        control.setTitle (title);
        for (auto* child : control.getChildren())
            if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child))
                toggle->setTooltip (tooltip);
    };
    describeSwitch (hold, "Pad Hold", "Keep cutting after you let go. Switch Hold off with nothing pressed to stop on the grid.");
    describeSwitch (straight, "Pad Straight", "Keeps every cut on even divisions of the beat: no triplets");

    for (auto* child : quantize.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
            button->setTooltip (button->getButtonText() == "Bar" ? "Releases and program changes land on the next bar"
                                                                  : "Releases and program changes land on the next beat");

    mix.setComponentID ("padMix");
    mix.setTitle ("Mix");
    mix.setTooltip ("How much of the program you hear while the pad plays. The same control as Wet on the Classic pages.");

    for (auto* component : std::initializer_list<juce::Component*> { &hold, &straight, &quantize, &mix })
        addAndMakeVisible (component);

    programAttachment.sendInitialUpdate();
}

engine::PadProgram PadPage::playingProgram (const DisplayState& display) const
{
    const auto active = static_cast<engine::PadProgram> (display.padActiveProgram.load());

    // While the pad plays, a switch waits for the grid. Idle, the chosen program plays next.
    if (display.padEngaged.load() || display.padPendingProgram.load() >= 0)
        return active;

    return engine::isAvailable (chosenProgram()) ? chosenProgram() : active;
}

bool PadPage::landsOnBar() const
{
    return state.getRawParameterValue (ids::padQuantize)->load() > 0.5f;
}

juce::String PadPage::programName (int index) const
{
    return programParameter.choices[index];
}

void PadPage::refresh (const DisplayState& display)
{
    const auto playing = static_cast<int> (playingProgram (display));
    const auto pending = display.padPendingProgram.load();

    for (int i = 0; i < programs.size(); ++i)
    {
        programs[i]->setToggleState (i == playing, juce::dontSendNotification);
        programs[i]->setPending (i == pending, pad.artMotion().beat);
    }

    const auto captions = engine::padCaptions (static_cast<engine::PadProgram> (playing));
    pad.setCaptions (captions.x, captions.y);
    pad.setProgram (playing);
    pad.showEngaged (display.padEngaged.load());

    // While it cuts, the host is so many units into the phrase, and a bar's units are so many of its beats.
    const auto unitsPerBar = display.unitsPerBar.load();
    const auto cutting = display.status.load() == DisplayState::Status::cutting && unitsPerBar > 0;
    pad.followBeat (cutting ? std::optional (HostBeat { display.position.load() * display.beatsPerBar.load() / unitsPerBar,
                                                        display.bpm.load() / 60.0 })
                            : std::nullopt);

    // The pad answers each block of cuts that starts while it plays. An editor opened mid-block has seen none start.
    const auto blockStart = display.blockStart.load();
    if (cutting && display.padEngaged.load() && lastBlockStart.has_value() && ! juce::exactlyEqual (blockStart, *lastBlockStart))
        pad.pulse();

    lastBlockStart = blockStart;
}

juce::String PadPage::headline (const DisplayState& display, const juce::String& status) const
{
    const auto pending = display.padPendingProgram.load();

    if (pending >= 0)
        return programName (display.padActiveProgram.load()) + " " + rightArrow + " " + programName (pending)
             + (landsOnBar() ? " on the bar" : " on the beat");

    // Playing but not cutting: nothing starts until a touch, and a touch waits for the next unit.
    if (display.status.load() == DisplayState::Status::startingSoon)
        return display.padEngaged.load() ? "Starting" : "Press the pad to play";

    return status;
}

void PadPage::resized()
{
    auto area = getLocalBounds();
    programRow.setBounds (area.removeFromTop (tabHeight));

    // The gaps between tabs absorb the rounding, so the row is exactly as wide as the sheet.
    const auto count = programs.size();
    const auto tabWidth = static_cast<float> (programRow.getWidth() - (count - 1) * tabGap) / static_cast<float> (count);

    for (int i = 0; i < count; ++i)
    {
        const auto left = juce::roundToInt (static_cast<float> (i) * (tabWidth + static_cast<float> (tabGap)));
        const auto right = juce::roundToInt (static_cast<float> (i) * (tabWidth + static_cast<float> (tabGap)) + tabWidth);
        programs[i]->setBounds (left, 0, right - left, tabHeight);
    }

    // Everything in the bottom row sits on one centre line.
    auto row = area.removeFromBottom (rowHeight);
    area.removeFromBottom (gap);
    pad.setBounds (area);

    mix.setBounds (row.removeFromRight (280));
    hold.setBounds (row.removeFromLeft (76));
    row.removeFromLeft (controlGap);
    straight.setBounds (row.removeFromLeft (104));
    row.removeFromLeft (controlGap);
    quantize.setBounds (row.removeFromLeft (128).reduced (0, 2));
}

void PadPage::visibilityChanged()
{
    // Out of sight the pad lets go, and goes back to its own slow pulse.
    if (! isVisible())
    {
        pad.endInteraction();
        pad.showEngaged (false);
        pad.followBeat (std::nullopt);
    }
}

} // namespace livecut::ui
