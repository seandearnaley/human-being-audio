// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "DisplayState.h"
#include "engine/PadPrograms.h"
#include "ui/Controls.h"
#include "ui/XYPad.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

namespace livecut::ui
{

// The pad surface: eight program tabs, the pad they open onto, then Hold,
// Straight, Beat / Bar and Mix. Where a control owns a parameter, its
// component ID is the parameter's ID. Mix is the Wet parameter under its own ID.
class PadPage final : public juce::Component
{
public:
    // A tab like a divider in a ring binder: dark, with its name in its
    // program's colour, until that program plays, when it stands taller in
    // the colour itself and runs into the pad's sheet. While it waits for the
    // grid it lights on every beat.
    class ProgramButton final : public juce::Button
    {
    public:
        ProgramButton (const juce::String& name, juce::Colour colour);

        // `beat` is the pad's beat count: a waiting tab is brightest on each beat.
        void setPending (bool pending, float beat);
        bool isPending() const { return pending; }

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::Colour colour;
        float lit = 0.0f; // how brightly a waiting tab is lit just now
        bool pending = false;
    };

    explicit PadPage (juce::AudioProcessorValueTreeState& state);

    // Follows the processor's display: which program plays or waits, the
    // pad's look and captions, its pointer mark, and a ring for each new cut.
    void refresh (const DisplayState& display);

    // The program whose tab is lit and whose colour the surface wears.
    engine::PadProgram playingProgram (const DisplayState& display) const;

    // The ruler's line: a pending switch, an invitation to press the pad or a
    // note that it's starting while the host plays but nothing is cut yet,
    // and otherwise the status.
    juce::String headline (const DisplayState& display, const juce::String& status) const;

    void resized() override;
    void visibilityChanged() override;

private:
    engine::PadProgram chosenProgram() const { return static_cast<engine::PadProgram> (chosen); }
    bool landsOnBar() const;
    juce::String programName (int index) const;

    juce::AudioProcessorValueTreeState& state;
    juce::AudioParameterChoice& programParameter;
    XYPad pad;
    juce::Component programRow;
    juce::OwnedArray<ProgramButton> programs;
    juce::ParameterAttachment programAttachment;
    PowerSwitch hold;
    PowerSwitch straight;
    ProcedureSwitch quantize;
    SliderRow mix;
    int chosen = 0;
    std::optional<double> lastBlockStart; // nothing until the first look at the display

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadPage)
};

} // namespace livecut::ui
