// Design tokens for Pole Field. Shared studio chrome from hba::ui plus
// Pole Field's signature phosphor magenta (poles), cyan (sub lock & zeros),
// amber (resonance & inter-stage drive), and violet (movement lanes).
#pragma once

#include "pf/ui/model.h"

#include <hba/ui/theme.h>

#include <vector>

namespace pf::ui {

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

inline constexpr unsigned sub = 0xff3fd9e8;     // sub lock bypass & zeros
inline constexpr unsigned poles = 0xffe85bc4;   // 14-pole Z-plane morph & response
inline constexpr unsigned drive = 0xfff5a742;   // resonance & inter-stage ADAA drive
inline constexpr unsigned motion = 0xffa98bff;  // 4 tempo-locked movement lanes
inline constexpr unsigned alert = 0xffff5d5d;   // limiter / clipping
}

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

}
