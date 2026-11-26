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

class PillButton final : public visage::Frame {
  public:
    PillButton(Context& context, std::string label, std::function<void()> onClick)
        : context_(context), label_(std::move(label)), onClick_(std::move(onClick)) {}

    void setLabel(std::string label) {
        if (label == label_)
            return;
        label_ = std::move(label);
        redraw();
    }

    void setAccent(unsigned accent) {
        if (accent == accent_)
            return;
        accent_ = accent;
        redraw();
    }

    void setActive(bool active) {
        if (active == active_)
            return;
        active_ = active;
        redraw();
    }

    void setOnDragOut(std::function<void()> fn) { onDragOut_ = std::move(fn); }
    void setOnRightClick(std::function<void()> fn) { onRightClick_ = std::move(fn); }

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

    void mouseEnter(const visage::MouseEvent&) override {
        hovered_ = true;
        redraw();
    }
    void mouseExit(const visage::MouseEvent&) override {
        hovered_ = false;
        redraw();
    }
    void mouseDown(const visage::MouseEvent& e) override {
        if (e.shouldTriggerPopup()) {
            if (onRightClick_)
                onRightClick_();
            return;
        }
        pressed_ = true;
        dragged_ = false;
        redraw();
    }
    void mouseDrag(const visage::MouseEvent&) override {
        if (pressed_ && !dragged_ && onDragOut_) {
            dragged_ = true;
            pressed_ = false;
            redraw();
            onDragOut_();
        }
    }
    void mouseUp(const visage::MouseEvent& e) override {
        if (!pressed_)
            return;
        const bool inside = localBounds().contains(e.position);
        const bool wasDragged = dragged_;
        pressed_ = false;
        dragged_ = false;
        redraw();
        if (inside && !wasDragged && onClick_)
            onClick_();
    }

  private:
    Context& context_;
    std::string label_;
    std::function<void()> onClick_;
    std::function<void()> onDragOut_;
    std::function<void()> onRightClick_;
    unsigned accent_ = colour::textDim;
    bool active_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool dragged_ = false;
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
