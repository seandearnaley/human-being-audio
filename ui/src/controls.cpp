#include "controls.h"

#include <algorithm>
#include <cmath>

namespace tf::ui {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kDragRange = 180.0f;  // design pixels for a full sweep
constexpr float kFine = 0.1f;

std::string withoutUnit(std::string text) {
    const auto space = text.find(' ');
    return space == std::string::npos ? text : text.substr(0, space);
}

// Characters, not bytes: "×16" is three.
std::size_t codePoints(const std::string& text) {
    return static_cast<std::size_t>(std::count_if(
        text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xc0u) != 0x80u; }));
}
}

void drawLabel(visage::Canvas& canvas, const Context& context, const std::string& text, float x, float y, float width,
               float height, unsigned colourArgb, visage::Font::Justification justification, float designSize,
               Weight weight) {
    canvas.setColor(colourArgb);
    canvas.text(text, font(context, designSize, weight), justification, x, y, width, height);
}

void drawCurve(visage::Canvas& canvas, const Context& context, std::span<const float> values, float x, float y,
               float width, float height, unsigned colourArgb, float low, float high, float fillAlpha, float baseline,
               float designThickness) {
    if (values.size() < 2 || width <= 0.0f || height <= 0.0f) return;

    const auto toY = [&](float value) {
        return height * (1.0f - std::clamp((value - low) / (high - low), 0.0f, 1.0f));
    };
    const float step = width / static_cast<float>(values.size() - 1);

    visage::Path line;
    line.moveTo(0.0f, toY(values[0]));
    for (std::size_t i = 1; i < values.size(); ++i) line.lineTo(step * static_cast<float>(i), toY(values[i]));

    if (fillAlpha > 0.0f) {
        visage::Path area = line;
        area.lineTo(width, toY(baseline));
        area.lineTo(0.0f, toY(baseline));
        area.close();
        canvas.setColor(colour::withAlpha(colourArgb, fillAlpha));
        canvas.fill(area, x, y, width, height);
    }

    canvas.setColor(colourArgb);
    canvas.stroke(line, x, y, width, height, context.px(designThickness));
}

ParamFrame::ParamFrame(Context& context, ParamId id, unsigned accent)
    : context_(context), accent_(accent), id_(id), spec_(tf::spec(id)) {
    context_.live.push_back(this);
}

ParamFrame::~ParamFrame() { std::erase(context_.live, static_cast<Live*>(this)); }

void ParamFrame::tick() {
    if (value() != drawn_) redraw();
}

void ParamFrame::draw(visage::Canvas& canvas) {
    drawn_ = value();
    paint(canvas);
}

void ParamFrame::commit(float real) {
    context_.model.begin(id_);
    context_.model.set(id_, clampToRange(spec_, real));
    context_.model.end(id_);
    redraw();
}

void ParamFrame::nudge(float normalisedDelta) {
    if (spec_.kind == ParamKind::continuous)
        commit(fromNormalised(spec_, std::clamp(normalised() + normalisedDelta, 0.0f, 1.0f)));
    else
        commit(value() + (normalisedDelta > 0.0f ? 1.0f : -1.0f));
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
    if (e.repeatClickCount() >= 2) return commit(spec_.def);
    if (spec_.kind == ParamKind::toggle) return commit(value() >= 0.5f ? 0.0f : 1.0f);
    if (spec_.kind == ParamKind::choice) return showMenu();

    dragging_ = true;
    dragPosition_ = normalised();
    lastY_ = e.position.y;
    context_.model.begin(id_);
    redraw();
}

void ParamFrame::mouseDrag(const visage::MouseEvent& e) {
    if (!dragging_) return;
    const float step = (lastY_ - e.position.y) / context_.px(kDragRange) * (e.isShiftDown() ? kFine : 1.0f);
    lastY_ = e.position.y;
    dragPosition_ = std::clamp(dragPosition_ + step, 0.0f, 1.0f);
    context_.model.set(id_, fromNormalised(spec_, dragPosition_));
    redraw();
}

void ParamFrame::mouseUp(const visage::MouseEvent&) {
    if (!dragging_) return;
    dragging_ = false;
    context_.model.end(id_);
    redraw();
}

bool ParamFrame::mouseWheel(const visage::MouseEvent& e) {
    if (e.wheel_delta_y == 0.0f) return false;
    const float direction = e.wheel_delta_y > 0.0f ? 1.0f : -1.0f;
    nudge(direction * 0.01f * (e.isShiftDown() ? kFine : 1.0f));
    return true;
}

Knob::Face Knob::valueFace() const { return { withoutUnit(text()), normalised() }; }

void Knob::setFace(FaceProvider provider) {
    provider_ = std::move(provider);
    redraw();
}

void Knob::tick() {
    if (provider_ && face() != drawnFace_) redraw();
    ParamFrame::tick();
}

void Knob::paint(visage::Canvas& canvas) {
    drawnFace_ = face();
    const float labelHeight = context_.px(16.0f);
    const float diameter = std::min(width(), height() - labelHeight);
    const float x = (width() - diameter) * 0.5f;
    const float thickness = context_.px(3.0f);

    // A 270 degree sweep, open at the bottom.
    constexpr float sweep = 1.5f * kPi;
    constexpr float start = 0.75f * kPi;
    const float position = std::clamp(drawnFace_.position, 0.0f, 1.0f);
    const float origin = spec().min < 0.0f ? 0.5f : 0.0f;
    const float from = std::min(origin, position);
    const float to = std::max(origin, position);

    canvas.setColor(colour::track);
    canvas.roundedArc(x, 0.0f, diameter, thickness, start + sweep * 0.5f, sweep * 0.5f);
    if (to - from > 0.001f) {
        canvas.setColor(accent_);
        canvas.roundedArc(x, 0.0f, diameter, thickness, start + sweep * (from + to) * 0.5f,
                          sweep * (to - from) * 0.5f);
    }

    // Up to three characters sit at title size; longer ones ("2 bars") step down to clear the arc.
    const float textSize = codePoints(drawnFace_.text) > 3 ? type::small : type::title;
    drawLabel(canvas, context_, drawnFace_.text, 0.0f, 0.0f, width(), diameter, colour::text, visage::Font::kCenter,
              textSize, Weight::medium);
    drawLabel(canvas, context_, std::string(spec().name), 0.0f, height() - labelHeight, width(), labelHeight,
              active() ? colour::text : colour::textDim, visage::Font::kCenter);
}

Row::Row(Context& context, ParamId id, unsigned accent, std::string label, bool bar)
    : ParamFrame(context, id, accent), label_(label.empty() ? std::string(tf::spec(id).name) : std::move(label)),
      bar_(bar) {}

void Row::paint(visage::Canvas& canvas) {
    const float w = width();
    const float h = height();
    drawLabel(canvas, context_, label_, 0.0f, 0.0f, w, h, active() ? colour::text : colour::textDim);

    if (spec().kind == ParamKind::toggle) {
        const float pillWidth = context_.px(24.0f);
        const float pillHeight = context_.px(13.0f);
        const float inset = context_.px(2.0f);
        const float x = w - pillWidth;
        const float y = (h - pillHeight) * 0.5f;
        const bool on = value() >= 0.5f;
        canvas.setColor(on ? accent_ : colour::track);
        canvas.roundedRectangle(x, y, pillWidth, pillHeight, pillHeight * 0.5f);
        canvas.setColor(on ? colour::window : colour::textDim);
        const float dot = pillHeight - 2.0f * inset;
        canvas.circle(on ? x + pillWidth - inset - dot : x + inset, y + inset, dot);
        return;
    }

    float valueRight = w;
    if (spec().kind == ParamKind::choice) {
        // triangleDown takes the half-width: the triangle is 2 * half wide and half tall.
        const float half = context_.px(3.0f);
        canvas.setColor(active() ? colour::text : colour::textFaint);
        canvas.triangleDown(w - 2.0f * half, (h - half) * 0.5f, half);
        valueRight = w - 2.0f * half - context_.px(5.0f);
    }
    drawLabel(canvas, context_, text(), 0.0f, 0.0f, valueRight, h, colour::text, visage::Font::kRight, type::label,
              Weight::regular);

    if (bar_) {
        const float thickness = context_.px(2.0f);
        canvas.setColor(colour::track);
        canvas.roundedRectangle(0.0f, h - thickness, w, thickness, thickness * 0.5f);
        // A two-way value grows from the centre; a one-way value from the left.
        const float origin = spec().min < 0.0f ? 0.5f : 0.0f;
        const float from = std::min(origin, normalised());
        const float to = std::max(origin, normalised());
        canvas.setColor(accent_);
        canvas.roundedRectangle(w * from, h - thickness, w * (to - from), thickness, thickness * 0.5f);
    }
}

void drawChip(visage::Canvas& canvas, const Context& context, const std::string& text, float width, float height,
              unsigned textArgb) {
    drawPanel(canvas, context, 0.0f, 0.0f, width, height, colour::raised, metric::radiusSmall);
    drawLabel(canvas, context, text, 0.0f, 0.0f, width, height, textArgb, visage::Font::kCenter, type::small);
}

void Chip::setAccent(unsigned accent) {
    if (accent == accent_) return;
    accent_ = accent;
    redraw();
}

void Chip::paint(visage::Canvas& canvas) {
    drawChip(canvas, context_, text(), width(), height(), active() ? colour::text : accent_);
}

Button::Button(Context& context, std::string label, std::function<void()> onClick)
    : context_(context), label_(std::move(label)), onClick_(std::move(onClick)) {}

void Button::setLabel(std::string label) {
    if (label == label_) return;
    label_ = std::move(label);
    redraw();
}

void Button::setAccent(unsigned accent) {
    if (accent == accent_) return;
    accent_ = accent;
    redraw();
}

void Button::draw(visage::Canvas& canvas) {
    drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), pressed_ ? colour::track : colour::raised,
              metric::radiusSmall);
    drawLabel(canvas, context_, label_, 0.0f, 0.0f, width(), height(), hovered_ ? colour::text : accent_,
              visage::Font::kCenter);
}

void Button::mouseEnter(const visage::MouseEvent&) {
    hovered_ = true;
    redraw();
}

void Button::mouseExit(const visage::MouseEvent&) {
    hovered_ = false;
    redraw();
}

void Button::mouseDown(const visage::MouseEvent&) {
    pressed_ = true;
    redraw();
}

void Button::mouseUp(const visage::MouseEvent& e) {
    const bool inside = localBounds().contains(e.position);
    pressed_ = false;
    redraw();
    if (inside && onClick_) onClick_();
}

}
