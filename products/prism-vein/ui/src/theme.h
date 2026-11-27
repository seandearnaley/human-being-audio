// SPDX-License-Identifier: AGPL-3.0-or-later
// Prism Vein chromatic obsidian theme tokens.
#pragma once

#include "pv/ui/model.h"

#include <hba/ui/theme.h>

#include <algorithm>
#include <array>
#include <vector>

namespace pv::ui {

using hba::ui::Live;
using hba::ui::Weight;

struct Context {
    explicit Context(Model& m) : model(m) {}

    Model& model;
    float scale = 1.0f;
    std::vector<Live*> live;

    float px(float designPixels) const { return designPixels * scale; }
};

namespace colour {
using hba::ui::colour::edge;
using hba::ui::colour::panel;
using hba::ui::colour::raised;
using hba::ui::colour::text;
using hba::ui::colour::textDim;
using hba::ui::colour::textFaint;
using hba::ui::colour::track;
using hba::ui::colour::window;
using hba::ui::colour::withAlpha;

inline constexpr unsigned prism = 0xff38e8ffu;    // Primary Prism Cyan (Color / Lattice / Shift)
inline constexpr unsigned sub = 0xff3ee5a0u;      // Phase-locked Sub Emerald (Sub Lock / Output)
inline constexpr unsigned harmonic = 0xffa884ffu; // Harmonic Violet (Decay / Carrier / Shimmer)
inline constexpr unsigned tame = 0xfff5a638u;     // Warm Amber (Auto-Tame / Grit / Shield)
inline constexpr unsigned rose = 0xffff6b8bu;     // Tame reduction ribbon / Freeze glow
inline constexpr unsigned alert = 0xffff5d5du;    // Peak / clip / GR
inline constexpr unsigned onColour = 0xff090c12u; // High-contrast ink on lit pills

inline constexpr std::array<unsigned, 4> enginePalette{
    prism,    // Chord Lattice
    harmonic, // Carrier Morph
    tame,     // Harmonic Comb
    rose,     // Glass Freeze
};

inline unsigned engineAccent(int engineIdx) {
    return enginePalette[static_cast<std::size_t>(std::clamp(engineIdx, 0, 3))];
}

// Spectral note color gradient across the 12 pitch classes (emerald -> cyan -> violet -> amber).
inline unsigned pitchAccent(int pc) {
    static constexpr std::array<unsigned, 12> kPcPalette{
        0xff3ee5a0u, // C
        0xff39e6c4u, // C#
        0xff38e8ffu, // D
        0xff4fd2ffu, // D#
        0xff6cb8ffu, // E
        0xff889effu, // F
        0xffa884ffu, // F#
        0xffc47affu, // G
        0xffe074d8u, // G#
        0xffff6b8bu, // A
        0xfff98a5fu, // A#
        0xfff5a638u, // B
    };
    return kPcPalette[static_cast<std::size_t>((pc % 12 + 12) % 12)];
}
} // namespace colour

namespace metric {
using namespace hba::ui::metric;
}

namespace type {
using namespace hba::ui::type;
}

inline visage::Font font(const Context& context, float designSize, Weight weight = Weight::regular) {
    return hba::ui::fontPx(context.px(designSize), weight);
}

inline void drawPanel(visage::Canvas& canvas, const Context& context, float x, float y, float width, float height,
                      unsigned fill = colour::panel, float designRadius = metric::radius) {
    hba::ui::drawPanel(canvas, context.scale, x, y, width, height, fill, designRadius);
}

} // namespace pv::ui
