// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace livecut::ui
{

// The whole skin: paper, ink, one typeface and one colour for each pad
// program. PadArt.h draws each program's art in its colour.
//
// A juce::Colour is built when the plug-in loads, in no promised order, so
// nothing else at namespace scope may be built from these.
namespace palette
{
inline const juce::Colour paper { 0xfff4f4f1 };
inline const juce::Colour well { 0xffe9e9e5 };   // a section set into the paper
inline const juce::Colour rule { 0x24000000 };   // hairlines, tracks and outlines
inline const juce::Colour ink { 0xff000000 };
inline const juce::Colour inkMuted { 0x99000000 };
inline const juce::Colour accent { 0xffff6b1a }; // the original Livecut's orange, and Cut's colour
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
// an offcut. It fills the height of `area` from its left edge.
void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);

} // namespace livecut::ui
