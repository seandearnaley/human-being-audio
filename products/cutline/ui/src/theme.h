// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline 2.0 Visage 1.0 vibrant dark-chrome design tokens, 8-lane palette & 8-program XY jewel colors.
#pragma once

#include "cl/ui/model.h"

#include <hba/ui/theme.h>

#include <algorithm>
#include <array>
#include <vector>

namespace cl::ui {

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

inline constexpr unsigned onColour = 0xff0c0f15;  // Dark crisp text on bright colored tabs/sheets
inline constexpr unsigned iris = 0xff936eff;      // 0: SLICE electric iris
inline constexpr unsigned lavender = 0xffbaa3ff;  // Soft lavender (ghosts)
inline constexpr unsigned cyan = 0xff1cd8ee;      // 3: SWING electric cyan
inline constexpr unsigned teal = 0xff1fe4b5;      // 6: MORPH mint teal
inline constexpr unsigned amber = 0xffffa01e;     // 2: BOUNCE solar amber
inline constexpr unsigned vermilion = 0xffff4e3e; // 1: ROLL blaze vermilion
inline constexpr unsigned coral = 0xffff4593;     // 5: STUTTER hyper coral
inline constexpr unsigned emerald = 0xff28df8a;   // 4: POLY neon emerald
inline constexpr unsigned dropRed = 0xffff6a52;   // 7: DROP laser coral-red

inline constexpr std::array<unsigned, kDrumLaneCount> lane{
    vermilion, // 0: KICK
    iris,      // 1: SNARE
    lavender,  // 2: GHOST
    coral,     // 3: CLAP
    cyan,      // 4: CL HAT
    teal,      // 5: OP HAT
    amber,     // 6: PERC A
    emerald,   // 7: PERC B
};

inline constexpr std::array<unsigned, kPerfPadCount> pad{
    iris,      // 0: Slice
    vermilion, // 1: Roll
    amber,     // 2: Bounce
    cyan,      // 3: Swing
    emerald,   // 4: Poly
    coral,     // 5: Stutter
    teal,      // 6: Morph
    dropRed,   // 7: Drop
};

inline unsigned lerpArgb(unsigned a, unsigned b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto mixChan = [t](unsigned ca, unsigned cb) -> unsigned {
        return static_cast<unsigned>(std::lround(static_cast<float>(ca) + (static_cast<float>(cb) - static_cast<float>(ca)) * t));
    };
    return (mixChan((a >> 24) & 0xffu, (b >> 24) & 0xffu) << 24) |
           (mixChan((a >> 16) & 0xffu, (b >> 16) & 0xffu) << 16) |
           (mixChan((a >> 8) & 0xffu, (b >> 8) & 0xffu) << 8) | mixChan(a & 0xffu, b & 0xffu);
}

inline unsigned programArgb(int program) {
    return pad[static_cast<std::size_t>(std::clamp(program, 0, kPerfPadCount - 1))];
}
} // namespace colour

namespace metric {
using namespace hba::ui::metric;
} // namespace metric

namespace type {
using namespace hba::ui::type;
} // namespace type

inline visage::Font font(const Context& context, float designSize, Weight weight = Weight::regular) {
    return hba::ui::fontPx(context.px(designSize), weight);
}

inline void drawPanel(visage::Canvas& canvas, const Context& context, float x, float y, float width, float height,
                      unsigned fill = colour::panel, float designRadius = metric::radius) {
    hba::ui::drawPanel(canvas, context.scale, x, y, width, height, fill, designRadius);
}

} // namespace cl::ui
