// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/ui/model.h"

#include <hba/ui/theme.h>

#include <string>
#include <vector>

namespace sb::ui {

using hba::ui::Live;
using hba::ui::Weight;

namespace colour {
using namespace hba::ui::colour;

// Deep obsidian-glass chassis & surface gradients
inline constexpr unsigned obsidianBgTop = 0xff0c101b;
inline constexpr unsigned obsidianBgBot = 0xff06080f;
inline constexpr unsigned cardTop = 0xff161e30;
inline constexpr unsigned cardBot = 0xff0d121d;
inline constexpr unsigned cardEdge = 0xff27334d;
inline constexpr unsigned wellTop = 0xff0b101c;
inline constexpr unsigned wellBot = 0xff060910;
inline constexpr unsigned wellEdge = 0xff1f2a42;
inline constexpr unsigned capTop = 0xff242f47;
inline constexpr unsigned capBot = 0xff0f1523;

// High-vibrance signature section accents
inline constexpr unsigned body = 0xff1ce8ff;     // Electric Cyan — Fundamental Body
inline constexpr unsigned bodyAlt = 0xff0095ff;  // Deep Azure pair
inline constexpr unsigned shell = 0xffff6a24;    // Solar Tangerine — 24-Mode Shell
inline constexpr unsigned shellAlt = 0xffff2e63; // Hot Magma pair
inline constexpr unsigned wire = 0xffe4f3ff;     // Plasma Ice — Wire & Click
inline constexpr unsigned wireAlt = 0xff5eb8ff;  // Electric Sky pair
inline constexpr unsigned punch = 0xffffbc2e;    // Luminous Gold — Disperser & Drive
inline constexpr unsigned smack = 0xffff3d9a;    // Electric Magenta — SMACK & MUTATE
inline constexpr unsigned locked = 0xff24f5a8;   // Neon Mint — Phase Lock, TIGHT, Safe
inline constexpr unsigned violet = 0xffa77bff;   // Luminous Violet — Glass Bell / Room
inline constexpr unsigned warn = 0xffff4769;     // Laser Coral — Mute / Heavy GR / Cancel
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

// Rich luminous obsidian-glass card with vertical surface sheen, subtle ambient top tint, and glowing rim accent!
inline void drawCard(visage::Canvas& canvas, const Context& ctx, float x, float y, float w, float h,
                     unsigned accent = colour::body, float radius = metric::radius, bool showAccentBar = true) {
    const float r = ctx.px(radius);
    canvas.setBrush(visage::Brush::vertical(colour::cardTop, colour::cardBot));
    canvas.roundedRectangle(x, y, w, h, r);

    // Soft colored ambient light spill down from top edge
    if (showAccentBar) {
        const float glowH = std::min(h * 0.45f, ctx.px(56.0f));
        canvas.setBrush(visage::Brush::vertical(colour::withAlpha(accent, 0.13f), colour::withAlpha(accent, 0.0f)));
        canvas.topRoundedRectangle(x, y, w, glowH, r);
    }

    // Crisp rim border + subtle top-edge sheen
    canvas.setColor(colour::cardEdge);
    canvas.roundedRectangleBorder(x, y, w, h, r, ctx.px(1.0f));

    if (showAccentBar) {
        const float barW = w - ctx.px(24.0f);
        const float barX = x + ctx.px(12.0f);
        const float barH = ctx.px(2.4f);
        canvas.setColor(colour::withAlpha(accent, 0.45f));
        canvas.roundedRectangleShadow(barX, y, barW, barH, barH * 0.5f, ctx.px(10.0f));
        canvas.setColor(accent);
        canvas.roundedRectangle(barX, y, barW, barH, barH * 0.5f);
    }
}

inline void drawPanel(visage::Canvas& canvas, const Context& ctx, float x, float y, float w, float h,
                      unsigned fill = colour::panel, float radius = metric::radius) {
    hba::ui::drawPanel(canvas, ctx.scale, x, y, w, h, fill, radius);
}

inline void drawLabel(visage::Canvas& canvas, const Context& ctx, const std::string& text, float x, float y, float w,
                      float h, unsigned argb = colour::textDim, visage::Font::Justification just = visage::Font::kLeft,
                      float size = type::label, Weight weight = Weight::medium) {
    hba::ui::drawLabel(canvas, ctx.scale, text, x, y, w, h, argb, just, size, weight);
}

} // namespace sb::ui
