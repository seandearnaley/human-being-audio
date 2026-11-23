// Design tokens. Shared studio chrome from hba::ui plus Tract Form's role colours.
#pragma once

#include "tf/ui/model.h"

#include <hba/ui/theme.h>

#include <vector>

namespace tf::ui {

using hba::ui::Live;
using hba::ui::Weight;

// Shared by every frame. Layout is authored at 1280x720 and multiplied by `scale`.
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

// Each hue means one thing.
inline constexpr unsigned master = 0xff3fd9e8;  // the master sine and the sub: the clean path
inline constexpr unsigned growl = 0xfff5a742;   // reese, tract, fold: the processed path
inline constexpr unsigned motion = 0xffa98bff;  // movement lanes and what they modulate
inline constexpr unsigned alert = 0xffff5d5d;   // limiter working, clipping
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
