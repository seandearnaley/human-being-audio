// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lc/ui/model.h"

#include <hba/ui/theme.h>
#include <visage/graphics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace lc::ui {

using hba::ui::Live;
using hba::ui::Weight;

namespace colour {
using namespace hba::ui::colour;

// Livecut's iconic signature orange + studio accents
inline constexpr unsigned accent = 0xffff6b1a;
inline constexpr unsigned cyan = 0xff34c9c5;
inline constexpr unsigned emerald = 0xff6cd04f;
inline constexpr unsigned amber = 0xfffcd21c;
inline constexpr unsigned rose = 0xfffa6e8c;
inline constexpr unsigned violet = 0xffab70f0;
inline constexpr unsigned blue = 0xff48a4f2;
inline constexpr unsigned onColour = 0xff090b0f;

// The 8 iconic Pad program colours (exact original Livecut Redux palette)
inline constexpr std::array<unsigned, 8> program{
    0xffff6b1a, // 0: Cut     (Orange)
    0xfffa6e8c, // 1: Bounce  (Coral Pink)
    0xffec5fb3, // 2: Glitch  (Magenta)
    0xffab70f0, // 3: Suction (Violet)
    0xff48a4f2, // 4: Filter  (Sky Blue)
    0xff34c9c5, // 5: Tape    (Teal)
    0xff6cd04f, // 6: Loops   (Lime Green)
    0xfffcd21c, // 7: Rush    (Gold)
};

inline unsigned programArgb(int index) { return program[static_cast<std::size_t>(std::clamp(index, 0, 7))]; }

inline unsigned lerpArgb(unsigned a, unsigned b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto mixByte = [t](unsigned ca, unsigned cb) {
        return static_cast<unsigned>(
            std::lround(static_cast<float>(ca) + (static_cast<float>(cb) - static_cast<float>(ca)) * t));
    };
    const unsigned oa = mixByte((a >> 24) & 0xffu, (b >> 24) & 0xffu);
    const unsigned or_ = mixByte((a >> 16) & 0xffu, (b >> 16) & 0xffu);
    const unsigned og = mixByte((a >> 8) & 0xffu, (b >> 8) & 0xffu);
    const unsigned ob = mixByte(a & 0xffu, b & 0xffu);
    return (oa << 24) | (or_ << 16) | (og << 8) | ob;
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
    visage::Font font(float designSize, Weight weight = Weight::regular) const {
        return hba::ui::fontPx(designSize * scale, weight);
    }

    Model& model;
    float scale = 1.0f;
    std::vector<Live*> live;
};

inline void drawPanel(visage::Canvas& canvas, const Context& context, float x, float y, float width, float height,
                      unsigned fill = colour::panel, float designRadius = metric::radius) {
    hba::ui::drawPanel(canvas, context.scale, x, y, width, height, fill, designRadius);
}

inline void drawLabel(visage::Canvas& canvas, const Context& context, const std::string& text, float x, float y,
                      float width, float height, unsigned colourArgb = colour::textDim,
                      visage::Font::Justification justification = visage::Font::kLeft, float designSize = type::label,
                      Weight weight = Weight::medium) {
    hba::ui::drawLabel(canvas, context.scale, text, x, y, width, height, colourArgb, justification, designSize, weight);
}

} // namespace lc::ui
