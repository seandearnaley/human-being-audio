// SPDX-License-Identifier: AGPL-3.0-or-later
// Compact 2-lane optional CC / Velocity expression drawer for Cutline's bottom deck.
#include "panels.h"

#include "movement_edit.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cl::ui {

namespace {

unsigned targetAccent(LaneTarget target) {
    switch (target) {
    case LaneTarget::cc74Slide:
        return colour::cyan;
    case LaneTarget::cc1Mod:
        return colour::amber;
    case LaneTarget::cc11Expr:
        return colour::emerald;
    case LaneTarget::pressure:
        return colour::coral;
    case LaneTarget::cc71Res:
        return colour::iris;
    case LaneTarget::velocity:
        return colour::teal;
    case LaneTarget::pitchScoop:
        return colour::vermilion;
    }
    return colour::cyan;
}

class ExprCurveView final : public visage::Frame {
  public:
    ExprCurveView(Context& ctx, int lane) : ctx_(ctx), lane_(lane) {}

    void draw(visage::Canvas& canvas) override {
        const Params p = paramsOf(ctx_.model);
        const LaneCurves cs = curvesOf(ctx_.model);
        const bool on = p.on(laneParam(lane_, LaneField::on));
        const unsigned hue = targetAccent(laneTargetOf(p, lane_));

        drawPanel(canvas, ctx_, 0.0f, 0.0f, width(), height(), colour::withAlpha(colour::window, 0.85f), 5.0f);

        const float padX = ctx_.px(8.0f);
        const float padY = ctx_.px(8.0f);
        const float pw = width() - 2.0f * padX;
        const float ph = height() - 2.0f * padY;

        // Zero center guide line
        canvas.setColor(0x1effffff);
        canvas.rectangle(padX, padY + ph * 0.5f, pw, ctx_.px(1.0f));

        constexpr int kSamples = 96;
        std::array<float, kSamples> vals{};
        const double lenQ = laneLengthQuarters(p, lane_);
        for (int i = 0; i < kSamples; ++i) {
            const double phs = static_cast<double>(i) / static_cast<double>(kSamples - 1);
            vals[static_cast<std::size_t>(i)] = static_cast<float>(laneOutput(p, cs, lane_, phs * lenQ));
        }

        drawCurve(canvas, ctx_, vals, padX, padY, pw, ph, on ? hue : colour::textFaint, -1.0f, 1.0f, on ? 0.14f : 0.04f,
                  0.0f, 1.8f);

        // Live phase cursor
        const auto& vis = ctx_.model.visuals();
        if (vis.playing) {
            const float x = padX + vis.lanePhase[static_cast<std::size_t>(lane_)] * pw;
            canvas.setColor(colour::withAlpha(hue, 0.75f));
            canvas.rectangle(x, padY, ctx_.px(1.5f), ph);
        }

        // Editable custom knots when shape == Custom
        const auto shape = static_cast<LaneShape>(
            std::clamp(p.choice(laneParam(lane_, LaneField::shape)), 0, static_cast<int>(LaneShape::custom)));
        if (shape == LaneShape::custom) {
            const Curve c = ctx_.model.laneCurve(lane_);
            const edit::Area area{padX, padY, pw, ph};
            for (int i = 0; i < c.count; ++i) {
                const auto& pt = c.points[static_cast<std::size_t>(i)];
                const float kx = edit::toX(area, pt.x);
                const float ky = edit::toY(area, pt.y);
                canvas.setColor(on ? hue : colour::textDim);
                canvas.circle(kx - ctx_.px(3.5f), ky - ctx_.px(3.5f), ctx_.px(7.0f));
            }
        }
    }

    void mouseDown(const visage::MouseEvent& e) override {
        const float padX = ctx_.px(8.0f);
        const float padY = ctx_.px(8.0f);
        const edit::Area area{padX, padY, width() - 2.0f * padX, height() - 2.0f * padY};
        const edit::Reach reach{ctx_.px(9.0f), ctx_.px(6.0f), ctx_.px(4.0f)};
        const Params p = paramsOf(ctx_.model);
        const auto shape = static_cast<LaneShape>(
            std::clamp(p.choice(laneParam(lane_, LaneField::shape)), 0, static_cast<int>(LaneShape::custom)));
        Curve c = shape == LaneShape::custom ? ctx_.model.laneCurve(lane_) : startingCurve(shape, lane_);
        if (shape != LaneShape::custom) {
            ctx_.model.begin(laneParam(lane_, LaneField::shape));
            ctx_.model.set(laneParam(lane_, LaneField::shape), static_cast<float>(LaneShape::custom));
            ctx_.model.end(laneParam(lane_, LaneField::shape));
        }
        if (gesture_.press(c, area, {e.position.x, e.position.y}, e.repeatClickCount(), e.isShiftDown(), reach))
            ctx_.model.setLaneCurve(lane_, c);
        redraw();
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        const float padX = ctx_.px(8.0f);
        const float padY = ctx_.px(8.0f);
        const edit::Area area{padX, padY, width() - 2.0f * padX, height() - 2.0f * padY};
        Curve c = ctx_.model.laneCurve(lane_);
        if (gesture_.drag(c, area, {e.position.x, e.position.y}, e.isShiftDown()))
            ctx_.model.setLaneCurve(lane_, c);
        redraw();
    }

    void mouseUp(const visage::MouseEvent&) override {
        const float padX = ctx_.px(8.0f);
        const float padY = ctx_.px(8.0f);
        const edit::Area area{padX, padY, width() - 2.0f * padX, height() - 2.0f * padY};
        Curve c = ctx_.model.laneCurve(lane_);
        if (gesture_.release(c, area, false))
            ctx_.model.setLaneCurve(lane_, c);
        redraw();
    }

  private:
    Context& ctx_;
    int lane_ = 0;
    edit::Gesture gesture_{};
};

class ExprLanesPanel final : public Panel {
  public:
    explicit ExprLanesPanel(Context& context) : Panel(context) {
        for (int l = 0; l < kLaneCount; ++l) {
            auto& row = lanes_[static_cast<std::size_t>(l)];
            const unsigned hue = l == 0 ? colour::cyan : colour::teal;
            row.on = &add<Chip>(context, laneParam(l, LaneField::on), hue);
            row.target = &add<Chip>(context, laneParam(l, LaneField::target), hue);
            row.rate = &add<Chip>(context, laneParam(l, LaneField::rate), colour::text);
            row.shape = &add<Chip>(context, laneParam(l, LaneField::shape), colour::text);
            row.depth = &add<Chip>(context, laneParam(l, LaneField::depth), hue);
            row.view = &add<ExprCurveView>(context, l);
        }
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        drawLabel(canvas, context_, "EXPRESSION MODULATION (CC / VELOCITY \u00b7 DRAG CURVE TO SHAPE)", px(12.0f),
                  px(8.0f), width() - px(24.0f), px(15.0f), colour::textDim, visage::Font::kLeft, 9.5f,
                  Weight::semibold);
    }

    void tick() override {
        if (!isVisible())
            return;
        for (auto& row : lanes_)
            row.view->redraw();
    }

    void resized() override {
        const float padX = px(12.0f);
        const float topY = px(28.0f);
        const float padB = px(10.0f);
        const float gap = px(8.0f);
        const float availH = std::max(px(60.0f), height() - topY - padB);
        const float rowH = (availH - gap) * 0.5f;
        const float ctlH = px(22.0f);

        for (int l = 0; l < kLaneCount; ++l) {
            auto& row = lanes_[static_cast<std::size_t>(l)];
            const float y = topY + static_cast<float>(l) * (rowH + gap);
            float x = padX;
            const float cy = y + (rowH - ctlH) * 0.5f;
            row.on->setBounds(x, cy, px(42.0f), ctlH);
            x += px(46.0f);
            row.target->setBounds(x, cy, px(90.0f), ctlH);
            x += px(94.0f);
            row.rate->setBounds(x, cy, px(58.0f), ctlH);
            x += px(62.0f);
            row.shape->setBounds(x, cy, px(74.0f), ctlH);
            x += px(78.0f);
            row.depth->setBounds(x, cy, px(54.0f), ctlH);
            x += px(60.0f);
            row.view->setBounds(x, y, std::max(px(80.0f), width() - padX - x), rowH);
        }
    }

  private:
    struct LaneRow {
        Chip* on = nullptr;
        Chip* target = nullptr;
        Chip* rate = nullptr;
        Chip* shape = nullptr;
        Chip* depth = nullptr;
        ExprCurveView* view = nullptr;
    };
    std::array<LaneRow, kLaneCount> lanes_{};
};

} // namespace

std::unique_ptr<Panel> makeMovementPanel(Context& context) { return std::make_unique<ExprLanesPanel>(context); }

} // namespace cl::ui
