// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace livecut::ui
{

// What moves a program's art. The pad keeps it; the art is drawn from it and
// from nothing else, so the same motion always draws the same picture.
struct ArtMotion
{
    float time = 0.0f;        // seconds of motion so far: unhurried at rest, quicker while the pad plays
    float spin = 0.0f;        // the same clock, but it brakes to a stop at each cut and runs back up
    float x = 0.5f, y = 0.5f; // the pad's two axes, eased so the art glides after the pointer mark
    float kick = 0.0f;        // 1 at each new cut, falling back to 0
    int cuts = 0;             // how many cuts so far, for art that rearranges itself on each one
};

// A program's art: pieces of card in tones of its colour, filling `area`. Each
// picture shows what its program does, and the pad's axes move it the way
// they move the sound.
void drawProgramArt (juce::Graphics&, int program, juce::Rectangle<float> area, const ArtMotion&);

// A pair of paper scissors cutting a strip of tape, for the About sheet and
// the app icon. `open` runs from 0, shut, to 1.
void drawScissors (juce::Graphics&, juce::Rectangle<float> area, float open);

} // namespace livecut::ui
