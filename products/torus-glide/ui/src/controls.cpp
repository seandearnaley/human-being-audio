// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"

#include <algorithm>
#include <cmath>

namespace tg::ui {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kDragRange = 170.0f;
constexpr float kFine = 0.12f;

std::string stripUnit(std::string s) {
    const auto sp = s.find(' ');
    return sp == std::string::npos ? s : s.substr(0, sp);
}
} // namespace

ParamFrame::ParamFrame(Context& ctx, ParamId id, unsigned accent)
    : context_(ctx), accent_(accent), id_(id), spec_(specOf(id)) {
    context_.live.push_back(this);
}

ParamFrame::~ParamFrame() { std::erase(context_.live, static_cast<Live*>(this)); }

void ParamFrame::tick() {
    if (value() != drawn_)
        redraw();
}

void ParamFrame::draw(visage::Canvas& canvas) {
    drawn_ = value();
    paint(canvas);
}

void ParamFrame::commit(float real) {
    context_.model.begin(id_);
    context_.model.set(id_, clampValue(spec_, real));
    context_.model.end(id_);
    redraw();
}

void ParamFrame::nudge(float delta) {
    if (spec_.kind == ParamKind::continuous)
        commit(fromNormalised(spec_, std::clamp(normalised() + delta, 0.0f, 1.0f)));
    else
        commit(value() + (delta > 0.0f ? 1.0f : -1.0f));
}

void ParamFrame::showMenu() {
    visage::PopupMenu menu;
    for (std::size_t i = 0; i < spec_.choices.size(); ++i)
        menu.addOption(static_cast<int>(i), std::string(spec_.choices[i]));
    menu.onSelection() = [this](int index) { commit(static_cast<float>(index)); };
    menu.show(this);
}

void ParamFrame::mouseEnter(const visage::MouseEvent&) {
    hovered_ = true;
    redraw();
}

void ParamFrame::mouseExit(const visage::MouseEvent&) {
    hovered_ = false;
    redraw();
}

void ParamFrame::mouseDown(const visage::MouseEvent& e) {
    if (e.shouldTriggerPopup())
        return;
    if (e.repeatClickCount() >= 2)
        return commit(spec_.def);
    if (spec_.kind == ParamKind::toggle)
        return commit(value() >= 0.5f ? 0.0f : 1.0f);
    if (spec_.kind == ParamKind::choice)
        return showMenu();

    dragging_ = true;
    dragPos_ = normalised();
    lastY_ = e.position.y;
    context_.model.begin(id_);
    redraw();
}

void ParamFrame::mouseDrag(const visage::MouseEvent& e) {
    if (!dragging_)
        return;
    const float step = (lastY_ - e.position.y) / context_.px(kDragRange) * (e.isShiftDown() ? kFine : 1.0f);
    lastY_ = e.position.y;
    dragPos_ = std::clamp(dragPos_ + step, 0.0f, 1.0f);
    context_.model.set(id_, fromNormalised(spec_, dragPos_));
    redraw();
}

void ParamFrame::mouseUp(const visage::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    context_.model.end(id_);
    redraw();
}

bool ParamFrame::mouseWheel(const visage::MouseEvent& e) {
    if (e.wheel_delta_y == 0.0f)
        return false;
    nudge((e.wheel_delta_y > 0.0f ? 0.015f : -0.015f) * (e.isShiftDown() ? kFine : 1.0f));
    return true;
}

// ---------- Knob ----------
Knob::Knob(Context& ctx, ParamId id, unsigned accent, std::string shortLabel)
    : ParamFrame(ctx, id, accent), label_(shortLabel.empty() ? std::string(spec().name) : std::move(shortLabel)) {}

void Knob::tick() {
    ParamFrame::tick();
    if (modProvider_) {
        const float live = std::clamp(modProvider_(), 0.0f, 1.0f);
        if (std::abs(live - drawnMod_) > 0.003f)
            redraw();
    }
}

void Knob::paint(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const float labelH = context_.px(15.0f);
    const float availH = std::max(12.0f, h - labelH - context_.px(2.0f));
    const float diam = std::min(w - context_.px(6.0f), availH);
    const float cx = w * 0.5f;
    const float cy = availH * 0.5f + context_.px(1.0f);
    const float thickness = context_.px(active() ? 3.4f : 2.8f);
    // Visage arc convention: 0 = 3 o'clock, -pi/2 = 12 o'clock (top), clockwise positive.
    const float topRad = -0.50f * kPi;
    const float startRad = -1.25f * kPi;
    const float sweepMax = 1.50f * kPi;

    const float pos = normalised();
    const float x = cx - diam * 0.5f;
    const float y = cy - diam * 0.5f;

    // Subtle dark knob plate
    canvas.setColor(active() ? colour::raised : colour::withAlpha(colour::raised, 0.7f));
    canvas.circle(x + thickness, y + thickness, std::max(2.0f, diam - 2.0f * thickness));

    // Background track arc (7:30 -> 12:00 -> 4:30, gap at 6 o'clock)
    canvas.setColor(colour::track);
    canvas.arc(x, y, diam, thickness, topRad, sweepMax * 0.5f, true);

    // Value arc (symmetric bipolar radiates from 12 o'clock; unipolar sweeps from 7:30)
    float midAngle = startRad;
    float halfSweep = 0.02f;
    const bool isSymmetricBipolar =
        (spec().low < 0.0f && spec().high > 0.0f && std::abs(spec().low + spec().high) < 0.01f);
    if (isSymmetricBipolar) {
        const float zeroNorm = toNormalised(spec(), 0.0f);
        const float a0 = startRad + zeroNorm * sweepMax;
        const float a1 = startRad + pos * sweepMax;
        midAngle = (a0 + a1) * 0.5f;
        halfSweep = std::max(0.025f, std::abs(a1 - a0) * 0.5f);
    } else {
        const float sweep = std::max(0.03f, pos * sweepMax);
        midAngle = startRad + sweep * 0.5f;
        halfSweep = sweep * 0.5f;
    }
    canvas.setColor(accent_);
    canvas.arc(x, y, diam, thickness, midAngle, halfSweep, true);

    // Optional live modulated indicator dot
    if (modProvider_) {
        drawnMod_ = std::clamp(modProvider_(), 0.0f, 1.0f);
        const float modAng = startRad + drawnMod_ * sweepMax;
        const float rad = diam * 0.5f - thickness * 0.5f;
        const float dx = cx + std::cos(modAng) * rad;
        const float dy = cy + std::sin(modAng) * rad;
        const float dot = context_.px(4.5f);
        canvas.setColor(colour::text);
        canvas.circle(dx - dot * 0.5f, dy - dot * 0.5f, dot);
    }

    // Center value text
    const std::string valTxt = active() ? text() : stripUnit(text());
    drawLabel(canvas, context_, valTxt, 0.0f, cy - context_.px(8.0f), w, context_.px(16.0f),
              active() ? colour::text : colour::withAlpha(colour::text, 0.92f), visage::Font::kCenter,
              valTxt.size() > 6 ? 9.5f : 11.0f, Weight::semibold);

    // Bottom caption
    drawLabel(canvas, context_, label_, 0.0f, h - labelH, w, labelH, active() ? colour::text : colour::textDim,
              visage::Font::kCenter, 10.2f, Weight::medium);
}

// ---------- Row ----------
Row::Row(Context& ctx, ParamId id, unsigned accent, std::string label, bool bar)
    : ParamFrame(ctx, id, accent), label_(label.empty() ? std::string(spec().name) : std::move(label)), bar_(bar) {}

void Row::paint(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const float pad = context_.px(6.0f);

    if (active()) {
        canvas.setColor(colour::withAlpha(colour::raised, 0.85f));
        canvas.roundedRectangle(0.0f, 0.0f, w, h, context_.px(metric::radiusSmall));
    }

    drawLabel(canvas, context_, label_, pad, 0.0f, w * 0.54f, h - context_.px(2.0f),
              active() ? colour::text : colour::textDim, visage::Font::kLeft, 10.5f, Weight::medium);

    if (spec().kind == ParamKind::toggle) {
        const bool on = value() >= 0.5f;
        const float sw = context_.px(28.0f);
        const float sh = context_.px(14.0f);
        const float sx = w - pad - sw;
        const float sy = (h - sh) * 0.5f;
        canvas.setColor(on ? colour::withAlpha(accent_, 0.35f) : colour::track);
        canvas.roundedRectangle(sx, sy, sw, sh, sh * 0.5f);
        canvas.setColor(on ? accent_ : colour::textFaint);
        canvas.roundedRectangleBorder(sx, sy, sw, sh, sh * 0.5f, context_.px(1.0f));
        const float knobD = sh - context_.px(4.0f);
        const float kx = on ? (sx + sw - knobD - context_.px(2.0f)) : (sx + context_.px(2.0f));
        canvas.setColor(on ? accent_ : colour::textDim);
        canvas.circle(kx, sy + context_.px(2.0f), knobD);
        return;
    }

    const std::string valStr = spec().kind == ParamKind::choice ? (text() + " ▾") : text();
    drawLabel(canvas, context_, valStr, w * 0.42f, 0.0f, w * 0.58f - pad, h - context_.px(2.0f),
              spec().kind == ParamKind::choice ? accent_
                                               : (active() ? colour::text : colour::withAlpha(colour::text, 0.9f)),
              visage::Font::kRight, 10.5f, Weight::semibold);

    if (bar_ && spec().kind == ParamKind::continuous) {
        const float bh = context_.px(2.0f);
        const float by = h - bh - context_.px(1.0f);
        const float bw = w - 2.0f * pad;
        canvas.setColor(colour::track);
        canvas.roundedRectangle(pad, by, bw, bh, bh * 0.5f);
        canvas.setColor(accent_);
        if (spec().low < 0.0f && spec().high > 0.0f) {
            const float midX = pad + bw * toNormalised(spec(), 0.0f);
            const float curX = pad + bw * normalised();
            canvas.roundedRectangle(std::min(midX, curX), by, std::max(context_.px(2.0f), std::abs(curX - midX)), bh,
                                    bh * 0.5f);
        } else {
            canvas.roundedRectangle(pad, by, std::max(context_.px(2.0f), bw * normalised()), bh, bh * 0.5f);
        }
    }
}

// ---------- Chip ----------
Chip::Chip(Context& ctx, ParamId id, unsigned accent, std::string prefix)
    : ParamFrame(ctx, id, accent), prefix_(std::move(prefix)) {}

void Chip::paint(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const bool isToggleOn = spec().kind == ParamKind::toggle && value() >= 0.5f;
    const unsigned fill = isToggleOn ? colour::withAlpha(accent_, 0.22f)
                                     : (active() ? colour::withAlpha(colour::raised, 1.0f) : colour::raised);
    drawPanel(canvas, context_, 0.0f, 0.0f, w, h, fill, metric::radiusSmall);
    if (isToggleOn || active()) {
        canvas.setColor(colour::withAlpha(accent_, isToggleOn ? 0.85f : 0.45f));
        canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, context_.px(metric::radiusSmall), context_.px(1.0f));
    }
    const std::string caption = prefix_.empty() ? text() : (prefix_ + " " + text());
    drawLabel(canvas, context_, caption, context_.px(4.0f), 0.0f, w - context_.px(8.0f), h,
              isToggleOn || spec().kind == ParamKind::choice ? accent_ : colour::text, visage::Font::kCenter, 10.2f,
              Weight::semibold);
}

// ---------- ActionButton ----------
ActionButton::ActionButton(Context& ctx, std::string label, std::function<void()> onClick)
    : context_(ctx), label_(std::move(label)), onClick_(std::move(onClick)) {}

void ActionButton::setLabel(std::string next) {
    if (label_ != next) {
        label_ = std::move(next);
        redraw();
    }
}

void ActionButton::setAccent(unsigned argb) {
    if (accent_ != argb) {
        accent_ = argb;
        redraw();
    }
}

void ActionButton::setActive(bool on) {
    if (active_ != on) {
        active_ = on;
        redraw();
    }
}

void ActionButton::mouseEnter(const visage::MouseEvent&) {
    hovered_ = true;
    redraw();
}

void ActionButton::mouseExit(const visage::MouseEvent&) {
    hovered_ = false;
    redraw();
}

void ActionButton::mouseDown(const visage::MouseEvent& e) {
    if (e.shouldTriggerPopup()) {
        if (onRightClick_)
            onRightClick_();
        return;
    }
    pressed_ = true;
    draggedOut_ = false;
    pressPos_ = e.position;
    redraw();
}

void ActionButton::mouseDrag(const visage::MouseEvent& e) {
    if (!pressed_ || draggedOut_ || !onDragOut_)
        return;
    const float dx = e.position.x - pressPos_.x;
    const float dy = e.position.y - pressPos_.y;
    if (dx * dx + dy * dy > context_.px(6.0f) * context_.px(6.0f)) {
        draggedOut_ = true;
        pressed_ = false;
        redraw();
        onDragOut_();
    }
}

void ActionButton::mouseUp(const visage::MouseEvent& e) {
    const bool fire = pressed_ && !draggedOut_ && localBounds().contains(e.position);
    pressed_ = false;
    draggedOut_ = false;
    redraw();
    if (fire && onClick_)
        onClick_();
}

void ActionButton::draw(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const unsigned fill = active_ ? colour::withAlpha(accent_, 0.24f)
                                  : (hovered_ ? colour::withAlpha(colour::track, 0.85f) : colour::raised);
    drawPanel(canvas, context_, 0.0f, 0.0f, w, h, fill, metric::radiusSmall);
    if (active_ || hovered_) {
        canvas.setColor(colour::withAlpha(accent_, active_ ? 0.9f : 0.45f));
        canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, context_.px(metric::radiusSmall), context_.px(1.0f));
    }
    drawLabel(canvas, context_, label_, context_.px(4.0f), 0.0f, w - context_.px(8.0f), h,
              active_ ? colour::text : accent_, visage::Font::kCenter, 10.5f, Weight::semibold);
}

} // namespace tg::ui
