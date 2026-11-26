// SPDX-License-Identifier: AGPL-3.0-or-later
// Volcanic obsidian & bioluminescent mineral design tokens for Lineage.
#pragma once

#include "ln/ui/model.h"

#include <hba/ui/theme.h>

#include <algorithm>
#include <array>
#include <vector>

namespace ln::ui {

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

inline constexpr unsigned sculpt = 0xfff5a638u;   // Molten amber — Sculpt / ADAA warmth
inline constexpr unsigned reese = 0xff3be2f0u;    // Glacial cyan — 7-voice Reese & Sync
inline constexpr unsigned strata = 0xffec58c6u;   // Phosphor magenta — Gen-2 Resampled Strata
inline constexpr unsigned sub = 0xff3ee5a0u;      // Sub emerald — Clean mono sub & dispersion
inline constexpr unsigned abyss = 0xffa884ffu;    // Deep violet — Free-Hz movement & abyss
inline constexpr unsigned alert = 0xffff5d5du;    // Limiter / clip
inline constexpr unsigned onColour = 0xff090c12u; // Dark crisp ink on luminous tabs/badges

inline constexpr std::array<unsigned, kPadProgramCount> padPalette{
    sculpt, // 0 Sculpt
    reese,  // 1 Reese
    strata, // 2 Strata
    sub,    // 3 Disperse
    abyss,  // 4 Abyss
};

inline unsigned padAccent(int program) {
    return padPalette[static_cast<std::size_t>(std::clamp(program, 0, kPadProgramCount - 1))];
}

inline unsigned lerpArgb(unsigned a, unsigned b, float t) {
    const float u = std::clamp(t, 0.0f, 1.0f);
    const auto mix = [&](int shift) -> unsigned {
        const float ca = static_cast<float>((a >> shift) & 0xffu);
        const float cb = static_cast<float>((b >> shift) & 0xffu);
        return static_cast<unsigned>(std::clamp(ca + (cb - ca) * u + 0.5f, 0.0f, 255.0f)) << shift;
    };
    return mix(24) | mix(16) | mix(8) | mix(0);
}

inline unsigned scaleRgb(unsigned c, float factor) {
    const auto ch = [&](int shift) -> unsigned {
        const float v = static_cast<float>((c >> shift) & 0xffu) * factor;
        return static_cast<unsigned>(std::clamp(v + 0.5f, 0.0f, 255.0f)) << shift;
    };
    return (c & 0xff000000u) | ch(16) | ch(8) | ch(0);
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

} // namespace ln::ui
