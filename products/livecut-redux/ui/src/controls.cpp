// SPDX-License-Identifier: GPL-3.0-or-later
#include "controls.h"

#include <algorithm>
#include <cmath>

namespace lc::ui {

// --- ActionButton -----------------------------------------------------------

void ActionButton::draw(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const float r = context_.px(metric::radiusSmall);
    const unsigned fill = active_                ? colour::withAlpha(accent_, 0.22f)
                          : pressed_             ? colour::track
                          : hovered_ && enabled_ ? colour::lerpArgb(colour::raised, colour::track, 0.5f)
                                                 : colour::raised;
    const unsigned border = active_                ? colour::withAlpha(accent_, 0.85f)
                            : hovered_ && enabled_ ? colour::withAlpha(accent_, 0.45f)
                                                   : colour::edge;
    const unsigned textCol = !enabled_ ? colour::textFaint : active_ ? accent_ : accent_;

    canvas.setColor(fill);
    canvas.roundedRectangle(0.0f, 0.0f, w, h, r);
    canvas.setColor(border);
    canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, r, context_.px(metric::hairline));
    drawLabel(canvas, context_, label_, context_.px(4.0f), 0.0f, w - context_.px(8.0f), h, textCol,
              visage::Font::kCenter, type::label, active_ ? Weight::semibold : Weight::medium);
}

// --- TogglePill -------------------------------------------------------------

TogglePill::TogglePill(Context& context, std::string paramId, std::string label, unsigned accent)
    : context_(context), id_(std::move(paramId)), label_(std::move(label)), accent_(accent) {
    context_.live.push_back(this);
}

TogglePill::~TogglePill() { std::erase(context_.live, static_cast<Live*>(this)); }

void TogglePill::setAccent(unsigned accent) {
    if (accent_ != accent) {
        accent_ = accent;
        redraw();
    }
}

void TogglePill::setDimmed(bool dimmed) {
    if (dimmed_ != dimmed) {
        dimmed_ = dimmed;
        redraw();
    }
}

void TogglePill::tick() {
    const bool on = context_.model.getNormalised(id_) >= 0.5f;
    if (!initialised_ || on != drawnOn_) {
        initialised_ = true;
        drawnOn_ = on;
        redraw();
    }
}

void TogglePill::draw(visage::Canvas& canvas) {
    const bool on = context_.model.getNormalised(id_) >= 0.5f;
    const float w = width();
    const float h = height();
    const float r = context_.px(metric::radiusSmall);
    const float alpha = dimmed_ ? 0.45f : 1.0f;

    canvas.setColor(on         ? colour::withAlpha(accent_, 0.18f * alpha)
                    : hovered_ ? colour::lerpArgb(colour::raised, colour::track, 0.45f)
                               : colour::raised);
    canvas.roundedRectangle(0.0f, 0.0f, w, h, r);
    canvas.setColor(on ? colour::withAlpha(accent_, 0.75f * alpha) : colour::edge);
    canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, r, context_.px(metric::hairline));

    const float dot = context_.px(6.0f);
    const float dotX = context_.px(9.0f);
    const float dotY = (h - dot) * 0.5f;
    canvas.setColor(on ? colour::withAlpha(accent_, alpha) : colour::withAlpha(colour::textFaint, 0.65f * alpha));
    canvas.circle(dotX, dotY, dot);

    drawLabel(canvas, context_, label_, dotX + dot + context_.px(6.0f), 0.0f, w - dotX - dot - context_.px(10.0f), h,
              colour::withAlpha(on ? colour::text : colour::textDim, alpha), visage::Font::kLeft, type::label,
              on ? Weight::semibold : Weight::medium);
}

void TogglePill::mouseEnter(const visage::MouseEvent&) {
    hovered_ = true;
    redraw();
}

void TogglePill::mouseExit(const visage::MouseEvent&) {
    hovered_ = false;
    redraw();
}

void TogglePill::mouseDown(const visage::MouseEvent& e) {
    if (e.shouldTriggerPopup())
        return;
    const bool next = context_.model.getNormalised(id_) < 0.5f;
    context_.model.beginGesture(id_);
    context_.model.setNormalised(id_, next ? 1.0f : 0.0f);
    context_.model.endGesture(id_);
    drawnOn_ = next;
    redraw();
}

// --- ParamSlider ------------------------------------------------------------

ParamSlider::ParamSlider(Context& context, std::string paramId, std::string label, unsigned accent, bool compact)
    : context_(context), id_(std::move(paramId)), label_(std::move(label)), accent_(accent), compact_(compact) {
    setName(label_);
    setAcceptsKeystrokes(true);
    onDraw() += [this](visage::Canvas& canvas) { hba::ui::drawKeyboardFocus(canvas, *this, context_.scale); };
    context_.live.push_back(this);
}

ParamSlider::~ParamSlider() { std::erase(context_.live, static_cast<Live*>(this)); }

void ParamSlider::setAccent(unsigned accent) {
    if (accent_ != accent) {
        accent_ = accent;
        redraw();
    }
}

void ParamSlider::setDimmed(bool dimmed) {
    if (dimmed_ != dimmed) {
        dimmed_ = dimmed;
        redraw();
    }
}

void ParamSlider::tick() {
    const float v = context_.model.getNormalised(id_);
    if (std::abs(v - drawn_) > 1.0e-4f) {
        drawn_ = v;
        redraw();
    }
}

void ParamSlider::draw(visage::Canvas& canvas) {
    const float v = std::clamp(context_.model.getNormalised(id_), 0.0f, 1.0f);
    drawn_ = v;
    const float w = width();
    const float h = height();
    const float alpha = dimmed_ ? 0.42f : 1.0f;

    if (compact_) {
        const float r = context_.px(metric::radiusSmall);
        canvas.setColor(colour::raised);
        canvas.roundedRectangle(0.0f, 0.0f, w, h, r);
        const float fillW = std::max(0.0f, (w - 2.0f) * v);
        if (fillW > 1.0f) {
            canvas.setColor(colour::withAlpha(accent_, (hovered_ || dragging_ ? 0.34f : 0.24f) * alpha));
            canvas.roundedRectangle(1.0f, 1.0f, fillW, h - 2.0f, r - 1.0f);
        }
        canvas.setColor(hovered_ || dragging_ ? colour::withAlpha(accent_, 0.65f * alpha) : colour::edge);
        canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, r, context_.px(metric::hairline));

        const float pad = context_.px(8.0f);
        drawLabel(canvas, context_, label_, pad, 0.0f, w * 0.5f, h, colour::withAlpha(colour::textDim, alpha),
                  visage::Font::kLeft, type::small, Weight::medium);
        drawLabel(canvas, context_, context_.model.formatValue(id_), w * 0.35f, 0.0f, w * 0.65f - pad, h,
                  colour::withAlpha(colour::text, alpha), visage::Font::kRight, type::small, Weight::semibold);
        return;
    }

    const float labelH = context_.px(15.0f);
    drawLabel(canvas, context_, label_, 0.0f, 0.0f, w * 0.62f, labelH,
              colour::withAlpha(hovered_ || dragging_ ? colour::text : colour::textDim, alpha), visage::Font::kLeft,
              type::small, Weight::medium);
    drawLabel(canvas, context_, context_.model.formatValue(id_), w * 0.45f, 0.0f, w * 0.55f, labelH,
              colour::withAlpha(colour::text, alpha), visage::Font::kRight, type::small, Weight::medium);

    const float trackH = context_.px(5.0f);
    const float trackY = labelH + (h - labelH - trackH) * 0.5f;
    const float trackR = trackH * 0.5f;
    const float padX = context_.px(5.0f);
    const float trackW = std::max(1.0f, w - 2.0f * padX);

    canvas.setColor(colour::track);
    canvas.roundedRectangle(padX, trackY, trackW, trackH, trackR);
    if (v > 0.002f) {
        canvas.setColor(colour::withAlpha(accent_, alpha));
        canvas.roundedRectangle(padX, trackY, trackW * v, trackH, trackR);
    }

    const float thumbD = context_.px(hovered_ || dragging_ ? 11.0f : 9.0f);
    const float tx = padX + trackW * v - thumbD * 0.5f;
    const float ty = trackY + trackH * 0.5f - thumbD * 0.5f;
    canvas.setColor(colour::withAlpha(colour::text, alpha));
    canvas.circle(tx, ty, thumbD);
    canvas.setColor(colour::withAlpha(accent_, 0.85f * alpha));
    canvas.ring(tx, ty, thumbD, context_.px(1.5f));
}

void ParamSlider::mouseEnter(const visage::MouseEvent&) {
    hovered_ = true;
    redraw();
}

void ParamSlider::mouseExit(const visage::MouseEvent&) {
    hovered_ = false;
    redraw();
}

void ParamSlider::setFromX(float x) {
    const float padX = compact_ ? context_.px(4.0f) : context_.px(5.0f);
    const float span = std::max(1.0f, width() - 2.0f * padX);
    const float norm = std::clamp((x - padX) / span, 0.0f, 1.0f);
    context_.model.setNormalised(id_, norm);
    drawn_ = norm;
    redraw();
}

void ParamSlider::mouseDown(const visage::MouseEvent& e) {
    if (e.shouldTriggerPopup())
        return;
    if (e.repeatClickCount() >= 2 || e.isAltDown()) {
        const float def = context_.model.defaultNormalised(id_);
        context_.model.beginGesture(id_);
        context_.model.setNormalised(id_, def);
        context_.model.endGesture(id_);
        drawn_ = def;
        redraw();
        return;
    }
    dragging_ = true;
    dragAnchorX_ = e.position.x;
    dragAnchorVal_ = context_.model.getNormalised(id_);
    context_.model.beginGesture(id_);
    if (!e.isShiftDown()) {
        setFromX(e.position.x);
        dragAnchorVal_ = context_.model.getNormalised(id_);
    }
}

void ParamSlider::mouseDrag(const visage::MouseEvent& e) {
    if (!dragging_)
        return;
    if (e.isShiftDown()) {
        const float span = std::max(1.0f, width());
        const float next = std::clamp(dragAnchorVal_ + (e.position.x - dragAnchorX_) / span * 0.12f, 0.0f, 1.0f);
        context_.model.setNormalised(id_, next);
        drawn_ = next;
        redraw();
    } else {
        setFromX(e.position.x);
        dragAnchorX_ = e.position.x;
        dragAnchorVal_ = context_.model.getNormalised(id_);
    }
}

void ParamSlider::mouseUp(const visage::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    context_.model.endGesture(id_);
    redraw();
}

bool ParamSlider::mouseWheel(const visage::MouseEvent& e) {
    const float step = e.isShiftDown() ? 0.005f : 0.025f;
    setFromNormalised(context_.model.getNormalised(id_) + e.wheel_delta_y * step);
    return true;
}

bool ParamSlider::keyPress(const visage::KeyEvent& key) {
    using Key = visage::KeyCode;
    const auto k = key.keyCode();
    if (key.isMainModifier() || (k != Key::Left && k != Key::Down && k != Key::Right && k != Key::Up))
        return false;
    const float step = key.isShiftDown() ? 0.005f : 0.025f;
    setFromNormalised(context_.model.getNormalised(id_) + ((k == Key::Left || k == Key::Down) ? -step : step));
    return true;
}

void ParamSlider::setFromNormalised(float v) {
    const auto next = std::clamp(v, 0.0f, 1.0f);
    context_.model.beginGesture(id_);
    context_.model.setNormalised(id_, next);
    context_.model.endGesture(id_);
    drawn_ = next;
    redraw();
}

// --- RangeSlider ------------------------------------------------------------

RangeSlider::RangeSlider(Context& context, std::string lowId, std::string highId, std::string label, unsigned accent)
    : context_(context), lowId_(std::move(lowId)), highId_(std::move(highId)), label_(std::move(label)),
      accent_(accent) {
    context_.live.push_back(this);
}

RangeSlider::~RangeSlider() { std::erase(context_.live, static_cast<Live*>(this)); }

void RangeSlider::setDimmed(bool dimmed) {
    if (dimmed_ != dimmed) {
        dimmed_ = dimmed;
        redraw();
    }
}

void RangeSlider::tick() {
    const float lo = context_.model.getNormalised(lowId_);
    const float hi = context_.model.getNormalised(highId_);
    if (std::abs(lo - drawnLow_) > 1.0e-4f || std::abs(hi - drawnHigh_) > 1.0e-4f) {
        drawnLow_ = lo;
        drawnHigh_ = hi;
        redraw();
    }
}

void RangeSlider::draw(visage::Canvas& canvas) {
    const float lo = std::clamp(context_.model.getNormalised(lowId_), 0.0f, 1.0f);
    const float hi = std::clamp(context_.model.getNormalised(highId_), 0.0f, 1.0f);
    drawnLow_ = lo;
    drawnHigh_ = hi;

    const float w = width();
    const float h = height();
    const float alpha = dimmed_ ? 0.42f : 1.0f;
    const float labelH = context_.px(15.0f);

    drawLabel(canvas, context_, label_, 0.0f, 0.0f, w * 0.44f, labelH,
              colour::withAlpha(hovered_ || dragTarget_ != DragTarget::none ? colour::text : colour::textDim, alpha),
              visage::Font::kLeft, type::small, Weight::medium);

    const std::string loTxt = context_.model.formatValue(lowId_);
    const std::string hiTxt = context_.model.formatValue(highId_);
    const std::string rangeTxt = loTxt == hiTxt ? loTxt : (loTxt + " \u2013 " + hiTxt);
    drawLabel(canvas, context_, rangeTxt, w * 0.34f, 0.0f, w * 0.66f, labelH, colour::withAlpha(colour::text, alpha),
              visage::Font::kRight, type::small, Weight::medium);

    const float trackH = context_.px(5.0f);
    const float trackY = labelH + (h - labelH - trackH) * 0.5f;
    const float padX = context_.px(5.0f);
    const float trackW = std::max(1.0f, w - 2.0f * padX);
    const float a = std::min(lo, hi);
    const float b = std::max(lo, hi);

    canvas.setColor(colour::track);
    canvas.roundedRectangle(padX, trackY, trackW, trackH, trackH * 0.5f);
    canvas.setColor(colour::withAlpha(accent_, alpha));
    canvas.roundedRectangle(padX + trackW * a, trackY, std::max(context_.px(3.0f), trackW * (b - a)), trackH,
                            trackH * 0.5f);

    const float thumbD = context_.px(hovered_ || dragTarget_ != DragTarget::none ? 10.5f : 8.5f);
    const float ty = trackY + trackH * 0.5f - thumbD * 0.5f;
    for (float pos : {a, b}) {
        const float tx = padX + trackW * pos - thumbD * 0.5f;
        canvas.setColor(colour::withAlpha(colour::text, alpha));
        canvas.circle(tx, ty, thumbD);
        canvas.setColor(colour::withAlpha(accent_, 0.85f * alpha));
        canvas.ring(tx, ty, thumbD, context_.px(1.5f));
    }
}

void RangeSlider::mouseEnter(const visage::MouseEvent&) {
    hovered_ = true;
    redraw();
}

void RangeSlider::mouseExit(const visage::MouseEvent&) {
    hovered_ = false;
    redraw();
}

void RangeSlider::mouseDown(const visage::MouseEvent& e) {
    if (e.shouldTriggerPopup())
        return;
    if (e.repeatClickCount() >= 2 || e.isAltDown()) {
        context_.model.beginGesture(lowId_);
        context_.model.beginGesture(highId_);
        context_.model.setNormalised(lowId_, context_.model.defaultNormalised(lowId_));
        context_.model.setNormalised(highId_, context_.model.defaultNormalised(highId_));
        context_.model.endGesture(lowId_);
        context_.model.endGesture(highId_);
        redraw();
        return;
    }

    const float padX = context_.px(5.0f);
    const float trackW = std::max(1.0f, width() - 2.0f * padX);
    const float click = std::clamp((e.position.x - padX) / trackW, 0.0f, 1.0f);
    anchorLow_ = context_.model.getNormalised(lowId_);
    anchorHigh_ = context_.model.getNormalised(highId_);
    anchorX_ = e.position.x;

    const float lo = std::min(anchorLow_, anchorHigh_);
    const float hi = std::max(anchorLow_, anchorHigh_);
    const float dLo = std::abs(click - lo);
    const float dHi = std::abs(click - hi);
    if (hi - lo > 0.14f && click > lo + 0.04f && click < hi - 0.04f)
        dragTarget_ = DragTarget::span;
    else
        dragTarget_ = dLo <= dHi ? DragTarget::low : DragTarget::high;

    context_.model.beginGesture(lowId_);
    context_.model.beginGesture(highId_);
    mouseDrag(e);
}

void RangeSlider::mouseDrag(const visage::MouseEvent& e) {
    if (dragTarget_ == DragTarget::none)
        return;
    const float padX = context_.px(5.0f);
    const float trackW = std::max(1.0f, width() - 2.0f * padX);
    const float scale = e.isShiftDown() ? 0.15f : 1.0f;

    if (dragTarget_ == DragTarget::span) {
        const float delta = ((e.position.x - anchorX_) / trackW) * scale;
        const float span = std::max(0.0f, anchorHigh_ - anchorLow_);
        const float newLo = std::clamp(anchorLow_ + delta, 0.0f, 1.0f - span);
        context_.model.setNormalised(lowId_, newLo);
        context_.model.setNormalised(highId_, newLo + span);
    } else if (dragTarget_ == DragTarget::low) {
        const float target = e.isShiftDown()
                                 ? std::clamp(anchorLow_ + ((e.position.x - anchorX_) / trackW) * scale, 0.0f, 1.0f)
                                 : std::clamp((e.position.x - padX) / trackW, 0.0f, 1.0f);
        const float curHi = context_.model.getNormalised(highId_);
        context_.model.setNormalised(lowId_, std::min(target, curHi));
    } else {
        const float target = e.isShiftDown()
                                 ? std::clamp(anchorHigh_ + ((e.position.x - anchorX_) / trackW) * scale, 0.0f, 1.0f)
                                 : std::clamp((e.position.x - padX) / trackW, 0.0f, 1.0f);
        const float curLo = context_.model.getNormalised(lowId_);
        context_.model.setNormalised(highId_, std::max(target, curLo));
    }
    redraw();
}

void RangeSlider::mouseUp(const visage::MouseEvent&) {
    if (dragTarget_ == DragTarget::none)
        return;
    dragTarget_ = DragTarget::none;
    context_.model.endGesture(lowId_);
    context_.model.endGesture(highId_);
    redraw();
}

// --- ChoiceRow --------------------------------------------------------------

ChoiceRow::ChoiceRow(Context& context, std::string paramId, std::string label, unsigned accent)
    : context_(context), id_(std::move(paramId)), label_(std::move(label)), accent_(accent) {
    context_.live.push_back(this);
}

ChoiceRow::~ChoiceRow() { std::erase(context_.live, static_cast<Live*>(this)); }

void ChoiceRow::setDimmed(bool dimmed) {
    if (dimmed_ != dimmed) {
        dimmed_ = dimmed;
        redraw();
    }
}

void ChoiceRow::tick() {
    const float v = context_.model.getNormalised(id_);
    if (std::abs(v - drawn_) > 1.0e-4f) {
        drawn_ = v;
        redraw();
    }
}

void ChoiceRow::draw(visage::Canvas& canvas) {
    drawn_ = context_.model.getNormalised(id_);
    const float w = width();
    const float h = height();
    const float r = context_.px(metric::radiusSmall);
    const float alpha = dimmed_ ? 0.42f : 1.0f;

    canvas.setColor(hovered_ ? colour::lerpArgb(colour::raised, colour::track, 0.45f) : colour::raised);
    canvas.roundedRectangle(0.0f, 0.0f, w, h, r);
    canvas.setColor(hovered_ ? colour::withAlpha(accent_, 0.6f * alpha) : colour::edge);
    canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, r, context_.px(metric::hairline));

    const float pad = context_.px(8.0f);
    if (!label_.empty()) {
        drawLabel(canvas, context_, label_, pad, 0.0f, w * 0.48f, h, colour::withAlpha(colour::textDim, alpha),
                  visage::Font::kLeft, type::small, Weight::medium);
        drawLabel(canvas, context_, context_.model.formatValue(id_) + " \u25be", w * 0.40f, 0.0f, w * 0.60f - pad, h,
                  colour::withAlpha(colour::text, alpha), visage::Font::kRight, type::small, Weight::semibold);
    } else {
        drawLabel(canvas, context_, context_.model.formatValue(id_) + " \u25be", pad, 0.0f, w - 2.0f * pad, h,
                  colour::withAlpha(colour::text, alpha), visage::Font::kCenter, type::small, Weight::semibold);
    }
}

void ChoiceRow::mouseEnter(const visage::MouseEvent&) {
    hovered_ = true;
    redraw();
}

void ChoiceRow::mouseExit(const visage::MouseEvent&) {
    hovered_ = false;
    redraw();
}

void ChoiceRow::showMenu() {
    const auto list = context_.model.choices(id_);
    if (list.empty())
        return;
    visage::PopupMenu menu;
    for (std::size_t i = 0; i < list.size(); ++i)
        menu.addOption(static_cast<int>(i), list[i]);
    menu.onSelection() = [this, n = list.size()](int index) {
        if (n <= 1)
            return;
        const float norm = static_cast<float>(index) / static_cast<float>(n - 1);
        context_.model.beginGesture(id_);
        context_.model.setNormalised(id_, norm);
        context_.model.endGesture(id_);
        drawn_ = norm;
        redraw();
    };
    menu.show(this);
}

void ChoiceRow::mouseDown(const visage::MouseEvent&) { showMenu(); }

bool ChoiceRow::mouseWheel(const visage::MouseEvent& e) {
    const auto list = context_.model.choices(id_);
    if (list.size() <= 1)
        return false;
    const int n = static_cast<int>(list.size());
    const int cur = static_cast<int>(std::lround(context_.model.getNormalised(id_) * static_cast<float>(n - 1)));
    const int next = std::clamp(cur + (e.wheel_delta_y > 0.0f ? 1 : -1), 0, n - 1);
    if (next == cur)
        return true;
    const float norm = static_cast<float>(next) / static_cast<float>(n - 1);
    context_.model.beginGesture(id_);
    context_.model.setNormalised(id_, norm);
    context_.model.endGesture(id_);
    drawn_ = norm;
    redraw();
    return true;
}

// --- SegmentedBar -----------------------------------------------------------

SegmentedBar::SegmentedBar(Context& context, std::vector<std::string> items, std::function<void(int)> onSelect,
                           unsigned accent)
    : context_(context), items_(std::move(items)), onSelect_(std::move(onSelect)), accent_(accent) {}

void SegmentedBar::setSelected(int index) {
    const int clamped = items_.empty() ? 0 : std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
    if (selected_ != clamped) {
        selected_ = clamped;
        redraw();
    }
}

void SegmentedBar::setAccent(unsigned accent) {
    if (accent_ != accent) {
        accent_ = accent;
        redraw();
    }
}

int SegmentedBar::indexAt(float x) const {
    if (items_.empty() || width() <= 0.0f)
        return -1;
    const int idx = static_cast<int>(x / width() * static_cast<float>(items_.size()));
    return std::clamp(idx, 0, static_cast<int>(items_.size()) - 1);
}

void SegmentedBar::draw(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const float r = context_.px(metric::radiusSmall);
    canvas.setColor(colour::raised);
    canvas.roundedRectangle(0.0f, 0.0f, w, h, r);
    canvas.setColor(colour::edge);
    canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, r, context_.px(metric::hairline));

    if (items_.empty())
        return;
    const float segW = w / static_cast<float>(items_.size());
    const float inset = context_.px(2.0f);

    for (std::size_t i = 0; i < items_.size(); ++i) {
        const float sx = segW * static_cast<float>(i);
        const bool isSel = static_cast<int>(i) == selected_;
        const bool isHov = static_cast<int>(i) == hovered_;

        if (isSel) {
            canvas.setColor(colour::withAlpha(accent_, 0.24f));
            canvas.roundedRectangle(sx + inset, inset, segW - 2.0f * inset, h - 2.0f * inset, r - inset);
            canvas.setColor(colour::withAlpha(accent_, 0.72f));
            canvas.roundedRectangleBorder(sx + inset, inset, segW - 2.0f * inset, h - 2.0f * inset, r - inset,
                                          context_.px(metric::hairline));
        } else if (isHov) {
            canvas.setColor(colour::withAlpha(colour::track, 0.8f));
            canvas.roundedRectangle(sx + inset, inset, segW - 2.0f * inset, h - 2.0f * inset, r - inset);
        }

        drawLabel(canvas, context_, items_[i], sx, 0.0f, segW, h,
                  isSel   ? colour::text
                  : isHov ? colour::text
                          : colour::textDim,
                  visage::Font::kCenter, type::small, isSel ? Weight::semibold : Weight::medium);
    }
}

void SegmentedBar::mouseMove(const visage::MouseEvent& e) {
    const int idx = indexAt(e.position.x);
    if (idx != hovered_) {
        hovered_ = idx;
        redraw();
    }
}

void SegmentedBar::mouseExit(const visage::MouseEvent&) {
    if (hovered_ != -1) {
        hovered_ = -1;
        redraw();
    }
}

void SegmentedBar::mouseDown(const visage::MouseEvent& e) {
    const int idx = indexAt(e.position.x);
    if (idx >= 0 && idx != selected_) {
        selected_ = idx;
        redraw();
        if (onSelect_)
            onSelect_(idx);
    }
}

// --- ParamSegmented ---------------------------------------------------------

ParamSegmented::ParamSegmented(Context& context, std::string paramId, std::vector<std::string> labels, unsigned accent)
    : context_(context), id_(std::move(paramId)), count_(std::max(1, static_cast<int>(labels.size()))),
      bar_(
          context, std::move(labels),
          [this](int idx) {
              const float norm = count_ > 1 ? static_cast<float>(idx) / static_cast<float>(count_ - 1) : 0.0f;
              context_.model.beginGesture(id_);
              context_.model.setNormalised(id_, norm);
              context_.model.endGesture(id_);
              drawn_ = idx;
          },
          accent) {
    addChild(&bar_);
    context_.live.push_back(this);
    tick();
}

ParamSegmented::~ParamSegmented() { std::erase(context_.live, static_cast<Live*>(this)); }

void ParamSegmented::setAccent(unsigned accent) { bar_.setAccent(accent); }

void ParamSegmented::tick() {
    const int idx =
        count_ > 1 ? static_cast<int>(std::lround(context_.model.getNormalised(id_) * static_cast<float>(count_ - 1)))
                   : 0;
    if (idx != drawn_) {
        drawn_ = idx;
        bar_.setSelected(idx);
    }
}

void ParamSegmented::resized() { bar_.setBounds(0.0f, 0.0f, width(), height()); }

} // namespace lc::ui
