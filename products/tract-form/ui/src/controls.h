// The controls every panel is assembled from. Each one edits exactly one
// parameter through the model and repaints only when that value changes.
#pragma once

#include "theme.h"

#include <hba/ui/controls.h>

#include <span>
#include <string>

namespace tf::ui {

using ParamFrame = hba::ui::ParamFrameBase<Context, ParamId, ParamSpec, ParamKind>;
using Knob = hba::ui::StandardKnob<ParamFrame>;
using Row = hba::ui::StandardRow<ParamFrame, ParamKind>;
using Chip = hba::ui::StandardChip<ParamFrame>;
using Button = hba::ui::StudioButton<Context>;

inline void drawChip(visage::Canvas& canvas, const Context& context, const std::string& text, float width, float height,
                     unsigned textArgb) {
    hba::ui::drawChip(canvas, context.scale, text, width, height, textArgb);
}

inline void drawLabel(visage::Canvas& canvas, const Context& context, const std::string& text, float x, float y,
                      float width, float height, unsigned colourArgb = colour::textDim,
                      visage::Font::Justification justification = visage::Font::kLeft, float designSize = type::label,
                      Weight weight = Weight::medium) {
    hba::ui::drawLabel(canvas, context.scale, text, x, y, width, height, colourArgb, justification, designSize, weight);
}

inline void drawCurve(visage::Canvas& canvas, const Context& context, std::span<const float> values, float x, float y,
                      float width, float height, unsigned colourArgb, float low = -1.0f, float high = 1.0f,
                      float fillAlpha = 0.0f, float baseline = 0.0f, float designThickness = metric::stroke) {
    hba::ui::drawCurve(canvas, context.scale, values, x, y, width, height, colourArgb, low, high, fillAlpha, baseline,
                       designThickness);
}

} // namespace tf::ui
