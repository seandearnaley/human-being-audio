// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline Visage 1.0 design tokens: shared studio chrome from hba::ui plus
// signature 262° electric iris (phrase statement/structure), oscillograph cyan
// (answer & glide), splice vermilion (ratchets & microtiming shift), amber
// (mutation), emerald (turnaround).
#pragma once

#include "cl/ui/model.h"

#include <hba/ui/theme.h>

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

inline constexpr unsigned iris = 0xff9b7eff;      // 262° electric iris (statement & core accent)
inline constexpr unsigned cyan = 0xff3fd9e8;      // answer, glide curves & CC74
inline constexpr unsigned amber = 0xfff5a742;     // mutation & density
inline constexpr unsigned vermilion = 0xffff6e5e; // ratchet bursts & microtiming stems
inline constexpr unsigned emerald = 0xff49d193;   // turnaround & active audition
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
