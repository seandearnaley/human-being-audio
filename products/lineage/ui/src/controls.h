// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "theme.h"

#include <hba/ui/controls.h>

#include <span>
#include <string>

namespace ln::ui {

using ParamFrame = hba::ui::ParamFrameBase<Context, ParamId, ParamSpec, ParamKind>;
using Knob = hba::ui::StandardKnob<ParamFrame>;
using Row = hba::ui::StandardRow<ParamFrame, ParamKind>;
using Chip = hba::ui::StandardChip<ParamFrame>;

class PillButton final : public hba::ui::ActionButtonBase<Context> {
  public:
    using ActionButtonBase::ActionButtonBase;
    void draw(visage::Canvas& canvas) override {
        const float px = context_.scale;
        const unsigned fill =
            active_ ? colour::withAlpha(accent_, pressed_ ? 0.92f : 0.85f)
                    : (pressed_ ? colour::track : (hovered_ ? colour::withAlpha(accent_, 0.16f) : colour::raised));
        const unsigned ink = active_ ? colour::onColour : (hovered_ ? colour::text : accent_);
        hba::ui::drawPanel(canvas, px, 0.0f, 0.0f, width(), height(), fill, metric::radiusSmall);
        if (active_ || hovered_) {
            canvas.setColor(colour::withAlpha(accent_, active_ ? 0.95f : 0.48f));
            canvas.roundedRectangleBorder(0.0f, 0.0f, width(), height(), metric::radiusSmall * px, 1.0f * px);
        }
        hba::ui::drawLabel(canvas, px, label_, 4.0f * px, 0.0f, width() - 8.0f * px, height(), ink,
                           visage::Font::kCenter, type::label, active_ ? Weight::semibold : Weight::medium);
    }
};

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

} // namespace ln::ui
