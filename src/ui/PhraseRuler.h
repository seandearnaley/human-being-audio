// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "DisplayState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace livecut::ui
{

// The phrase as a strip of tape, cut into its bars: the units marked along
// it, the block being cut in the accent colour, and the playhead. Until there
// is a phrase to show, a line of status text takes the tape's place; given
// room above the tape, the line stays there.
class PhraseRuler final : public juce::Component
{
public:
    struct Snapshot
    {
        DisplayState::Status status = DisplayState::Status::waitingForHost;
        int phraseBars = 0;
        int unitsPerBar = 8;
        double blockStart = 0.0;
        double blockEnd = 0.0;
        double position = 0.0;

        static Snapshot of (const DisplayState& state);

        // Bitwise: the display only needs repainting when something changed at all.
        bool sameAs (const Snapshot& other) const;
    };

    void show (const Snapshot& snapshot);

    // What the status line says for a snapshot.
    static juce::String describe (const Snapshot& snapshot);
    juce::String statusText() const { return describe (current); }

    // A line shown in place of the status text, such as the pad's program and
    // bar. Empty shows the status text.
    void setHeadline (const juce::String& text);
    const juce::String& headlineText() const { return headline; }

    // The colour of the block being cut.
    void setAccent (juce::Colour colour);

    void paint (juce::Graphics& g) override;

private:
    Snapshot current;
    juce::String headline;
    juce::Colour accent;
};

} // namespace livecut::ui
