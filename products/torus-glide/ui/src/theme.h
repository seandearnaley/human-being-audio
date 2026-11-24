// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/ui/model.h"

#include <hba/ui/theme.h>

#include <array>
#include <vector>

namespace tg::ui {

using hba::ui::Live;
using hba::ui::Weight;

namespace colour {
using namespace hba::ui::colour;

inline constexpr unsigned spiral = 0xff48c8f5;   // Cyan — Shepard-Risset spiral geometry
inline constexpr unsigned copedent = 0xffdf72e8; // Magenta — Polyphonic chord-bend matrix
inline constexpr unsigned mobius = 0xff4ee0a6;   // Emerald — Hybrid Mobius & Sub-Anchor floor
inline constexpr unsigned drive = 0xfff2a94b;    // Amber — ADAA2 saturation & filter

inline constexpr std::array<unsigned, kVoiceCount> voices{
    0xff4ee0a6, // Voice 1 (Root / Sub Anchor)
    0xff48c8f5, // Voice 2
    0xffa894ff, // Voice 3
    0xffdf72e8, // Voice 4
    0xfff2a94b, // Voice 5
};
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
