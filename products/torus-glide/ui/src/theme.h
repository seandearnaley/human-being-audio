// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/ui/model.h"

#include <hba/ui/theme.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tg::ui {

using hba::ui::Live;
using hba::ui::Weight;

namespace colour {
using namespace hba::ui::colour;

inline constexpr unsigned spiral = 0xff38c6f4;   // Electric Cyan — Shepard-Risset spiral geometry
inline constexpr unsigned copedent = 0xffdf6ae8; // Orchid Magenta — Polyphonic chord-bend matrix
inline constexpr unsigned mobius = 0xff38e0a4;   // Luminous Emerald — Hybrid Mobius & Sub-Anchor floor
inline constexpr unsigned drive = 0xfff4a83a;    // Solar Amber — ADAA2 saturation & Risset clockwork
inline constexpr unsigned abyss = 0xff7d8cff;    // Cobalt Indigo — Subterranean gravity well
inline constexpr unsigned voidRose = 0xfff06490; // Nebula Rose — Manifold FDN shimmer halo
inline constexpr unsigned onColour = 0xff090c11; // Crisp ink on luminous jewel tabs/bezel

inline constexpr std::array<unsigned, kVoiceCount> voices{
    0xff38e0a4, // Voice 1 (Root / Sub Anchor)
    0xff38c6f4, // Voice 2
    0xff9e8cff, // Voice 3
    0xffdf6ae8, // Voice 4
    0xfff4a83a, // Voice 5
};

inline constexpr std::array<unsigned, kPadProgramCount> padAccents{
    spiral,   // 0: Spiral
    mobius,   // 1: Mobius
    copedent, // 2: Copedent
    abyss,    // 3: Abyss
    drive,    // 4: Risset
    voidRose, // 5: Void
};

inline unsigned padAccent(int program) {
    return padAccents[static_cast<std::size_t>(std::clamp(program, 0, kPadProgramCount - 1))];
}

inline unsigned lerpArgb(unsigned a, unsigned b, float t) {
    const float u = std::clamp(t, 0.0f, 1.0f);
    const auto mix = [u](unsigned ca, unsigned cb) {
        return static_cast<unsigned>(
            std::lround(static_cast<float>(ca) + (static_cast<float>(cb) - static_cast<float>(ca)) * u));
    };
    const unsigned al = mix((a >> 24) & 0xffu, (b >> 24) & 0xffu);
    const unsigned r = mix((a >> 16) & 0xffu, (b >> 16) & 0xffu);
    const unsigned g = mix((a >> 8) & 0xffu, (b >> 8) & 0xffu);
    const unsigned bl = mix(a & 0xffu, b & 0xffu);
    return (al << 24) | (r << 16) | (g << 8) | bl;
}

inline unsigned scaleRgb(unsigned argb, float gain) {
    const float g = std::max(0.0f, gain);
    const unsigned al = (argb >> 24) & 0xffu;
    const auto sc = [g](unsigned ch) {
        return static_cast<unsigned>(std::clamp(std::lround(static_cast<float>(ch) * g), 0l, 255l));
    };
    return (al << 24) | (sc((argb >> 16) & 0xffu) << 16) | (sc((argb >> 8) & 0xffu) << 8) | sc(argb & 0xffu);
}
} // namespace colour

namespace metric {
using namespace hba::ui::metric;
}

namespace type {
using namespace hba::ui::type;
}

struct Context {
    explicit Context(Model& m) : model(m) {}

    float px(float design) const { return design * scale; }
    visage::Font font(float designSize, Weight w = Weight::regular) const {
        return hba::ui::fontPx(designSize * scale, w);
    }

    Model& model;
    float scale = 1.0f;
    std::vector<Live*> live;
};

inline void drawPanel(visage::Canvas& canvas, const Context& ctx, float x, float y, float w, float h,
                      unsigned fill = colour::panel, float radius = metric::radius) {
    hba::ui::drawPanel(canvas, ctx.scale, x, y, w, h, fill, radius);
}

inline void drawLabel(visage::Canvas& canvas, const Context& ctx, const std::string& text, float x, float y, float w,
                      float h, unsigned argb = colour::textDim, visage::Font::Justification just = visage::Font::kLeft,
                      float size = type::label, Weight weight = Weight::medium) {
    hba::ui::drawLabel(canvas, ctx.scale, text, x, y, w, h, argb, just, size, weight);
}

inline unsigned modeAccent(int modeIdx) {
    if (modeIdx == 1)
        return colour::copedent;
    if (modeIdx == 2)
        return colour::mobius;
    return colour::spiral;
}

} // namespace tg::ui
