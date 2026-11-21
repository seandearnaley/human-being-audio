// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace livecut::ui
{

// The whole skin: light on black, one typeface and one colour for each pad
// program. PadArt.h draws each program's art in its colour.
//
// A juce::Colour is built when the plug-in loads, in no promised order, so
// nothing else at namespace scope may be built from these.
namespace palette
{
inline const juce::Colour backdrop { 0xff000000 };
inline const juce::Colour well { 0xff161616 };     // a section raised from the backdrop
inline const juce::Colour rule { 0x2effffff };     // hairlines, tracks and outlines
inline const juce::Colour ink { 0xffffffff };      // text, thumbs, anything chosen
inline const juce::Colour inkMuted { 0x99ffffff };
inline const juce::Colour onColour { 0xff000000 }; // text and marks on a program's colour
inline const juce::Colour accent { 0xffff6b1a };   // the original Livecut's orange, and Cut's colour
} // namespace palette

namespace weight
{
inline constexpr float regular = 400.0f, medium = 500.0f, semibold = 600.0f, bold = 700.0f;
} // namespace weight

juce::Font uiFont (float height, float weight = weight::regular);

// Figures that all take the same width, for values that change while you watch.
juce::Font numberFont (float height);

// The wordmark's condensed extra-bold.
juce::Font displayFont (float height);

// Whether a font is one of the three above.
bool isSkinFont (const juce::Font&);

// The typeface is loaded while anything holds one of these, and unloaded when
// the last is let go. The look and feel holds one, so an open editor makes
// its fonts from a typeface that is already there.
std::shared_ptr<void> keepTypeface();

// A pad program's colour, by its index in the Pad Program parameter.
juce::Colour programColour (int program);

// LIVECUT, cut on the diagonal between LIVE and CUT, with CUT dropped like
// an offcut. It fills the height of `area` from its left edge. `knock` drops
// CUT further: 1 is as far as the blade sends it.
void drawWordmark (juce::Graphics&, juce::Rectangle<float> area, float knock = 0.0f);

// How far in from a line's start a font sets its first letter. Take it off, and lines of different sizes
// share a left edge.
float sideBearing (const juce::Font&, const juce::String& text);

} // namespace livecut::ui
