// SPDX-License-Identifier: AGPL-3.0-or-later
#include "panels.h"

#include <algorithm>

namespace ln::ui {

Panel::Panel(Context& context) : context_(context) { context_.live.push_back(this); }

Panel::~Panel() { std::erase(context_.live, static_cast<Live*>(this)); }

Params paramsOf(const Model& model) {
    Params p;
    for (int i = 0; i < kParamCount; ++i)
        p.values[static_cast<std::size_t>(i)] = model.get(static_cast<ParamId>(i));
    return p;
}

LaneCurves curvesOf(const Model& model) {
    LaneCurves curves;
    for (int lane = 0; lane < kLaneCount; ++lane)
        curves[static_cast<std::size_t>(lane)] = model.laneCurve(lane);
    return curves;
}

} // namespace ln::ui
