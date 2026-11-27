// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "theme.h"

#include <hba/ui/controls.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>
#include <string>

namespace pv::ui {

using ParamFrame = hba::ui::ParamFrameBase<Context, ParamId, ParamSpec, ParamKind>;
using Knob = hba::ui::StandardKnob<ParamFrame>;
using Row = hba::ui::StandardRow<ParamFrame, ParamKind>;
using Chip = hba::ui::StandardChip<ParamFrame>;

class PillButton final : public hba::ui::ActionButtonBase<Context> {
  public:
    using ActionButtonBase::ActionButtonBase;

    void setFontSize(float designSize) {
        fontSize_ = designSize;
        redraw();
    }

    void draw(visage::Canvas& canvas) override {
        const float px = context_.scale;
        const unsigned fill =
            active_ ? colour::withAlpha(accent_, pressed_ ? 0.92f : 0.86f)
                    : (pressed_ ? colour::track : (hovered_ ? colour::withAlpha(accent_, 0.16f) : colour::raised));
        const unsigned ink = active_ ? colour::onColour : (hovered_ ? colour::text : accent_);
        hba::ui::drawPanel(canvas, px, 0.0f, 0.0f, width(), height(), fill, metric::radiusSmall);
        if (active_ || hovered_) {
            canvas.setColor(colour::withAlpha(accent_, active_ ? 0.96f : 0.50f));
            canvas.roundedRectangleBorder(0.0f, 0.0f, width(), height(), metric::radiusSmall * px, 1.0f * px);
        }
        hba::ui::drawLabel(canvas, px, label_, 4.0f * px, 0.0f, width() - 8.0f * px, height(), ink,
                           visage::Font::kCenter, fontSize_, active_ ? Weight::semibold : Weight::medium);
    }

  private:
    float fontSize_ = type::label;
};

// Large sculptural luminous dial for the 6 hero macros in COLOR.
class HeroKnob final : public ParamFrame {
  public:
    using ParamFrame::ParamFrame;

  private:
    void paint(visage::Canvas& canvas) override {
        const float sc = context_.scale;
        const float w = width();
        const float h = height();
        const float titleH = 18.0f * sc;
        const float dialMax = std::min(w - 10.0f * sc, h - titleH - 4.0f * sc);
        const float d = std::max(38.0f * sc, dialMax);
        const float cx = w * 0.5f;
        const float cy = d * 0.5f + 2.0f * sc;
        const float rx = cx - d * 0.5f;
        const float ry = cy - d * 0.5f;

        // Recessed dark well
        canvas.setColor(colour::withAlpha(colour::window, 0.74f));
        canvas.circle(rx + 6.0f * sc, ry + 6.0f * sc, d - 12.0f * sc);
        canvas.setColor(colour::withAlpha(colour::edge, active() ? 0.95f : 0.65f));
        canvas.ring(rx + 6.0f * sc, ry + 6.0f * sc, d - 12.0f * sc, 1.0f * sc);

        constexpr float sweep = 1.5f * hba::ui::kUiPi;
        constexpr float start = 0.75f * hba::ui::kUiPi;
        const float pos = std::clamp(normalised(), 0.0f, 1.0f);
        const float origin = spec().low < 0.0f ? 0.5f : 0.0f;
        const float from = std::min(origin, pos);
        const float to = std::max(origin, pos);
        const float thick = 4.2f * sc;

        canvas.setColor(colour::track);
        canvas.roundedArc(rx, ry, d, thick, start + sweep * 0.5f, sweep * 0.5f);

        if (to - from > 0.002f) {
            canvas.setColor(colour::withAlpha(accent_, active() ? 0.24f : 0.14f));
            canvas.roundedArc(rx - 1.5f * sc, ry - 1.5f * sc, d + 3.0f * sc, thick + 3.0f * sc,
                              start + sweep * (from + to) * 0.5f, sweep * (to - from) * 0.5f);
            canvas.setColor(accent_);
            canvas.roundedArc(rx, ry, d, thick, start + sweep * (from + to) * 0.5f, sweep * (to - from) * 0.5f);
        }

        const float angle = start + sweep * pos;
        const float radius = d * 0.5f - thick * 0.5f;
        const float dot = 5.4f * sc;
        canvas.setColor(colour::text);
        canvas.circle(cx + radius * std::cos(angle) - dot * 0.5f, cy + radius * std::sin(angle) - dot * 0.5f, dot);

        hba::ui::drawLabel(canvas, sc, text(), 0.0f, ry, w, d, colour::text, visage::Font::kCenter, 12.4f,
                           Weight::semibold);

        std::string title(spec().name);
        for (char& ch : title) {
            if (ch >= 'a' && ch <= 'z')
                ch = static_cast<char>(ch - 'a' + 'A');
        }
        hba::ui::drawLabel(canvas, sc, title, 0.0f, h - titleH, w, titleH, active() ? colour::text : colour::textDim,
                           visage::Font::kCenter, 10.2f, Weight::semibold);
    }
};

inline void drawLabel(visage::Canvas& canvas, const Context& context, const std::string& text, float x, float y,
                      float width, float height, unsigned colourArgb = colour::textDim,
                      visage::Font::Justification justification = visage::Font::kLeft, float designSize = type::label,
                      Weight weight = Weight::medium) {
    hba::ui::drawLabel(canvas, context.scale, text, x, y, width, height, colourArgb, justification, designSize, weight);
}

inline void drawCurve(visage::Canvas& canvas, const Context& context, std::span<const float> values, float x, float y,
                      float width, float height, unsigned colourArgb, float low = 0.0f, float high = 1.0f,
                      float fillAlpha = 0.0f, float baseline = 0.0f, float designThickness = metric::stroke) {
    hba::ui::drawCurve(canvas, context.scale, values, x, y, width, height, colourArgb, low, high, fillAlpha, baseline,
                       designThickness);
}

} // namespace pv::ui
