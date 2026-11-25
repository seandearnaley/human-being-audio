// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"

#include <algorithm>
#include <cmath>

namespace sb::ui {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kDragRange = 165.0f;
constexpr float kFine = 0.12f;

std::string compactValue(const ParamSpec& spec, const std::string& full) {
    if (spec.id == ParamId::bodyHz) {
        // Show "185 F#3" cleanly inside circular knob cap at rest (full "185 Hz F#3" on hover/drag!)
        const auto hzPos = full.find(" Hz ");
        if (hzPos != std::string::npos)
            return full.substr(0, hzPos) + " " + full.substr(hzPos + 4);
        return full;
    }
    if (spec.id == ParamId::pitchDrop || spec.id == ParamId::outputDb)
        return full;
    const auto sp = full.find(' ');
    return sp == std::string::npos ? full : full.substr(0, sp);
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

// ---------- Sculpted Luminous Hardware Knob ----------
Knob::Knob(Context& ctx, ParamId id, unsigned accent, std::string shortLabel, bool hero)
    : ParamFrame(ctx, id, accent), label_(shortLabel.empty() ? std::string(spec().name) : std::move(shortLabel)),
      hero_(hero) {}

void Knob::paint(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const float labelH = context_.px(hero_ ? 16.0f : 14.0f);
    const float availH = std::max(14.0f, h - labelH - context_.px(2.0f));
    const float diam = std::min(w - context_.px(hero_ ? 8.0f : 6.0f), availH);
    const float cx = w * 0.5f;
    const float cy = availH * 0.5f + context_.px(1.0f);
    const float thickness = context_.px(hero_ ? (active() ? 4.8f : 4.2f) : (active() ? 3.8f : 3.2f));

    // Visage arc convention: 0 = 3 o'clock, -pi/2 = 12 o'clock (top), clockwise positive.
    const float topRad = -0.50f * kPi;
    const float startRad = -1.25f * kPi;
    const float sweepMax = 1.50f * kPi;

    const float pos = normalised();
    const float x = cx - diam * 0.5f;
    const float y = cy - diam * 0.5f;

    // Subtle radial ambient glow behind hero/active knobs
    if (hero_ || active()) {
        const float auraR = diam * (active() ? 0.72f : 0.62f);
        canvas.setBrush(visage::Brush::radial(colour::withAlpha(accent_, active() ? 0.24f : 0.13f),
                                              colour::withAlpha(accent_, 0.0f), {cx, cy}, auraR));
        canvas.circle(cx - auraR, cy - auraR, auraR * 2.0f);
    }

    // Recessed dark groove track
    canvas.setColor(0xff0b101b);
    canvas.circle(x + thickness * 0.5f, y + thickness * 0.5f, std::max(2.0f, diam - thickness));

    canvas.setColor(0xff202b42);
    canvas.arc(x, y, diam, thickness, topRad, sweepMax * 0.5f, true);

    // Compute value arc geometry (only symmetric bipolar params radiate from 12 o'clock; Output dB sweeps from 7:30!)
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

    // GPU Neon Bloom shadow behind the value arc!
    const float glowAmt = (hero_ ? 0.55f : 0.36f) + (active() ? 0.25f : 0.0f);
    canvas.setColor(colour::withAlpha(accent_, glowAmt));
    canvas.roundedArcShadow(x, y, diam, thickness, midAngle, halfSweep, context_.px(hero_ ? 12.0f : 8.0f));

    // Crisp luminous value arc
    canvas.setColor(accent_);
    canvas.arc(x, y, diam, thickness, midAngle, halfSweep, true);

    // Machined brushed hardware center cap with specular top bevel
    const float capInset = thickness + context_.px(hero_ ? 3.2f : 2.6f);
    const float capD = std::max(4.0f, diam - 2.0f * capInset);
    canvas.setBrush(visage::Brush::vertical(active() ? 0xff2b3854 : colour::capTop, colour::capBot));
    canvas.circle(x + capInset, y + capInset, capD);
    canvas.setColor(colour::withAlpha(active() ? accent_ : 0xff46597e, active() ? 0.65f : 0.45f));
    canvas.ring(x + capInset, y + capInset, capD, context_.px(1.0f));

    // Luminous indicator dot sitting on the dial perimeter at the live angle!
    const float valAng = startRad + pos * sweepMax;
    const float pipR = capD * 0.5f - context_.px(hero_ ? 4.5f : 3.6f);
    const float pxPos = cx + std::cos(valAng) * pipR;
    const float pyPos = cy + std::sin(valAng) * pipR;
    const float pipD = context_.px(hero_ ? 4.2f : 3.2f);
    canvas.setColor(colour::withAlpha(accent_, 0.55f));
    canvas.fadeCircle(pxPos - pipD, pyPos - pipD, pipD * 2.0f, pipD);
    canvas.setColor(0xffffffff);
    canvas.circle(pxPos - pipD * 0.5f, pyPos - pipD * 0.5f, pipD);

    // Center value readout
    const std::string fullTxt = text();
    const std::string valTxt = active() ? fullTxt : compactValue(spec(), fullTxt);
    const float fontSz =
        hero_ ? (valTxt.size() > 6 ? 10.6f : 12.2f) : (valTxt.size() > 8 ? 8.4f : (valTxt.size() > 6 ? 9.2f : 10.4f));
    drawLabel(canvas, context_, valTxt, 0.0f, cy - context_.px(8.0f), w, context_.px(16.0f), 0xfff4f8ff,
              visage::Font::kCenter, fontSz, Weight::semibold);

    // Bold uppercase-styled caption
    drawLabel(canvas, context_, label_, 0.0f, h - labelH, w, labelH,
              active() ? colour::text : (hero_ ? 0xffc6d4ec : colour::textDim), visage::Font::kCenter,
              hero_ ? 10.2f : 9.6f, hero_ ? Weight::semibold : Weight::medium);
}

// ---------- Sleek Pill Fader / Choice Chip / Toggle Badge ----------
Row::Row(Context& ctx, ParamId id, unsigned accent, std::string label, Style style)
    : ParamFrame(ctx, id, accent), label_(label.empty() ? std::string(spec().name) : std::move(label)), style_(style) {}

void Row::paint(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const float r = context_.px(5.0f);

    // 1. Compact glowing badge toggle (e.g., "Ø" polarity invert or "SUB MONO" / "LIMITER" chips!)
    if (style_ == Style::badge || spec().kind == ParamKind::toggle) {
        const bool on = value() >= 0.5f;
        if (on) {
            canvas.setColor(colour::withAlpha(accent_, 0.30f));
            canvas.roundedRectangleShadow(0.0f, 0.0f, w, h, r, context_.px(7.0f));
            canvas.setBrush(
                visage::Brush::vertical(colour::withAlpha(accent_, 0.30f), colour::withAlpha(accent_, 0.14f)));
        } else {
            canvas.setBrush(visage::Brush::vertical(active() ? 0xff1c2539 : 0xff111827, 0xff0b101b));
        }
        canvas.roundedRectangle(0.0f, 0.0f, w, h, r);
        canvas.setColor(on ? colour::withAlpha(accent_, 0.90f)
                           : (active() ? colour::withAlpha(accent_, 0.45f) : colour::wellEdge));
        canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, r, context_.px(1.0f));

        // Status LED dot on wider toggle badges
        if (w > context_.px(44.0f)) {
            const float dotD = context_.px(5.5f);
            const float dx = context_.px(8.0f);
            const float dy = (h - dotD) * 0.5f;
            canvas.setColor(on ? accent_ : 0xff3b4863);
            canvas.circle(dx, dy, dotD);
            drawLabel(canvas, context_, label_, dx + dotD + context_.px(5.0f), 0.0f, w - dx - dotD - context_.px(10.0f),
                      h, on ? 0xfff6faff : colour::textDim, visage::Font::kLeft, 9.5f, Weight::semibold);
        } else {
            drawLabel(canvas, context_, label_, 0.0f, 0.0f, w, h, on ? 0xffffffff : colour::textDim,
                      visage::Font::kCenter, 10.0f, Weight::semibold);
        }
        return;
    }

    // 2. Choice dropdown chip (tinted pill button with crisp vector chevron!)
    if (style_ == Style::chip || spec().kind == ParamKind::choice) {
        canvas.setBrush(
            visage::Brush::vertical(active() ? colour::withAlpha(accent_, 0.24f) : colour::withAlpha(accent_, 0.13f),
                                    colour::withAlpha(accent_, 0.06f)));
        canvas.roundedRectangle(0.0f, 0.0f, w, h, r);
        canvas.setColor(colour::withAlpha(accent_, active() ? 0.85f : 0.40f));
        canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, r, context_.px(1.0f));

        const float pad = context_.px(7.0f);
        const float triW = context_.px(6.0f);
        const float triH = context_.px(4.0f);
        const float tx = w - pad - triW;
        const float ty = (h - triH) * 0.5f;

        if (!label_.empty() && label_ != "-" && w > context_.px(180.0f)) {
            drawLabel(canvas, context_, label_, pad, 0.0f, w * 0.42f, h, colour::textDim, visage::Font::kLeft, 9.2f,
                      Weight::medium);
            drawLabel(canvas, context_, text(), w * 0.36f, 0.0f, w * 0.64f - pad - triW - context_.px(5.0f), h,
                      0xfff4f8ff, visage::Font::kRight, 9.8f, Weight::semibold);
        } else {
            drawLabel(canvas, context_, text(), pad, 0.0f, w - 2.0f * pad - triW - context_.px(4.0f), h, 0xfff4f8ff,
                      visage::Font::kLeft, 9.6f, Weight::semibold);
        }

        canvas.setColor(accent_);
        canvas.triangle(tx, ty, tx + triW, ty, tx + triW * 0.5f, ty + triH);
        return;
    }

    // 3. Tactile Filled-Pill Horizontal Fader (replaces flat 2px underlines!)
    canvas.setBrush(visage::Brush::vertical(active() ? 0xff131b2b : colour::wellTop, colour::wellBot));
    canvas.roundedRectangle(0.0f, 0.0f, w, h, r);

    const float pad = context_.px(2.0f);
    const float innerW = std::max(4.0f, w - 2.0f * pad);
    const float innerH = std::max(4.0f, h - 2.0f * pad);
    const float norm = normalised();

    float fillX = pad;
    float fillW = 0.0f;
    float cursorX = pad + innerW * norm;
    if (spec().low < 0.0f && spec().high > 0.0f) {
        const float midX = pad + innerW * toNormalised(spec(), 0.0f);
        fillX = std::min(midX, cursorX);
        fillW = std::max(context_.px(2.0f), std::abs(cursorX - midX));
    } else {
        fillX = pad;
        fillW = std::max(context_.px(2.0f), innerW * norm);
    }

    // Luminous gradient fill inside the pill track
    canvas.setBrush(visage::Brush::horizontal(colour::withAlpha(accent_, active() ? 0.18f : 0.11f),
                                              colour::withAlpha(accent_, active() ? 0.40f : 0.26f)));
    canvas.roundedRectangle(fillX, pad, fillW, innerH, context_.px(3.5f));

    // Bright glowing leading-edge cursor bar
    const float curW = context_.px(2.4f);
    const float clampedCur = std::clamp(cursorX - curW * 0.5f, pad, w - pad - curW);
    canvas.setColor(colour::withAlpha(accent_, active() ? 0.70f : 0.42f));
    canvas.roundedRectangleShadow(clampedCur, pad, curW, innerH, curW * 0.5f, context_.px(6.0f));
    canvas.setColor(accent_);
    canvas.roundedRectangle(clampedCur, pad, curW, innerH, curW * 0.5f);

    // Subtle crisp pill frame
    canvas.setColor(active() ? colour::withAlpha(accent_, 0.65f) : colour::wellEdge);
    canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, r, context_.px(1.0f));

    // Crisp left label + right value readout
    const float txtPad = context_.px(7.0f);
    drawLabel(canvas, context_, label_, txtPad, 0.0f, w * 0.56f, h, active() ? 0xffffffff : 0xffb8c7e0,
              visage::Font::kLeft, 9.6f, Weight::medium);
    drawLabel(canvas, context_, text(), w * 0.46f, 0.0f, w * 0.54f - txtPad, h, 0xfff6faff, visage::Font::kRight, 9.8f,
              Weight::semibold);
}

// ---------- Tactile Luminous ActionButton ----------
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

void ActionButton::setGradientAccent(unsigned fromArgb, unsigned toArgb) {
    accent_ = fromArgb;
    gradEnd_ = toArgb;
    redraw();
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

void ActionButton::mouseUp(const visage::MouseEvent&) {
    const bool fire = pressed_ && !draggedOut_;
    pressed_ = false;
    draggedOut_ = false;
    redraw();
    if (fire && onClick_)
        onClick_();
}

void ActionButton::draw(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    const float r = context_.px(metric::radiusSmall);

    if (active_) {
        const unsigned endCol = gradEnd_ != 0u ? gradEnd_ : accent_;
        canvas.setColor(colour::withAlpha(accent_, 0.36f));
        canvas.roundedRectangleShadow(0.0f, 0.0f, w, h, r, context_.px(10.0f));
        canvas.setBrush(visage::Brush::horizontal(colour::withAlpha(accent_, hovered_ ? 0.42f : 0.30f),
                                                  colour::withAlpha(endCol, hovered_ ? 0.34f : 0.22f)));
        canvas.roundedRectangle(0.0f, 0.0f, w, h, r);
        canvas.setColor(colour::withAlpha(accent_, 0.95f));
        canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, r, context_.px(1.2f));
    } else {
        canvas.setBrush(
            visage::Brush::vertical(hovered_ ? 0xff222d44 : 0xff182032, hovered_ ? 0xff161e30 : 0xff101624));
        canvas.roundedRectangle(0.0f, 0.0f, w, h, r);
        canvas.setColor(hovered_ ? colour::withAlpha(accent_, 0.70f) : colour::cardEdge);
        canvas.roundedRectangleBorder(0.0f, 0.0f, w, h, r, context_.px(1.0f));
    }

    drawLabel(canvas, context_, label_, context_.px(4.0f), 0.0f, w - context_.px(8.0f), h,
              active_ ? 0xffffffff : (hovered_ ? 0xfff4f8ff : accent_), visage::Font::kCenter, 10.0f, Weight::semibold);
}

} // namespace sb::ui
