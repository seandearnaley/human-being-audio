// SPDX-License-Identifier: AGPL-3.0-or-later
#include "controls.h"
#include "panels.h"

#include <hba/ui/movement_edit.h>

#include <algorithm>
#include <array>

namespace tg::ui {

namespace {
class LaneCard final : public visage::Frame, public Live {
  public:
    LaneCard(Context& ctx, int laneIndex, unsigned accent)
        : context_(ctx), lane_(laneIndex), accent_(accent),
          rateChip_(ctx, laneIndex == 0 ? ParamId::lane1Rate : ParamId::lane2Rate, accent),
          targetChip_(ctx, laneIndex == 0 ? ParamId::lane1Target : ParamId::lane2Target, accent),
          depthRow_(ctx, laneIndex == 0 ? ParamId::lane1Depth : ParamId::lane2Depth, accent, "Depth", true),
          btnUp_(ctx, "Ramp \xe2\x86\x91", [this] { context_.model.setCurve(lane_, presetRampUpCurve()); }),
          btnDn_(ctx, "Ramp \xe2\x86\x93", [this] { context_.model.setCurve(lane_, presetRampDownCurve()); }),
          btnTri_(ctx, "Tri", [this] { context_.model.setCurve(lane_, presetTriangleCurve()); }),
          btnBloom_(ctx, "Bloom", [this] { context_.model.setCurve(lane_, presetSBloomCurve()); }),
          btnStep_(ctx, "Step 4", [this] { context_.model.setCurve(lane_, presetStep4Curve()); }) {
        for (auto* b : {&btnUp_, &btnDn_, &btnTri_, &btnBloom_, &btnStep_}) {
            b->setAccent(colour::textDim);
            addChild(b);
        }
        addChild(&rateChip_);
        addChild(&targetChip_);
        addChild(&depthRow_);
        context_.live.push_back(this);
    }

    ~LaneCard() override { std::erase(context_.live, static_cast<Live*>(this)); }

    void tick() override { redraw(); }

    void resized() override {
        const auto px = [this](float v) { return context_.px(v); };
        const float pad = px(10.0f);
        const float topY = px(7.0f);
        const float ch = px(20.0f);

        targetChip_.setBounds(px(64.0f), topY, px(84.0f), ch);
        rateChip_.setBounds(px(152.0f), topY, px(64.0f), ch);
        depthRow_.setBounds(px(222.0f), topY, px(124.0f), ch);

        const float bw = px(48.0f);
        float rx = width() - pad;
        for (auto* b : {&btnStep_, &btnBloom_, &btnTri_, &btnDn_, &btnUp_}) {
            rx -= bw;
            b->setBounds(rx, topY, bw, ch);
            rx -= px(3.0f);
        }
    }

    void mouseDown(const visage::MouseEvent& e) override {
        const auto area = curveArea();
        if (e.position.x < area.x || e.position.x > area.x + area.width || e.position.y < area.y ||
            e.position.y > area.y + area.height)
            return;
        working_ = context_.model.curve(lane_);
        const hba::ui::edit::Reach reach{context_.px(9.0f), context_.px(6.0f), context_.px(3.0f)};
        if (gesture_.press(working_, area, {e.position.x, e.position.y}, e.repeatClickCount(), e.isShiftDown(),
                           reach)) {
            context_.model.setCurve(lane_, working_);
            redraw();
        }
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (!gesture_.active())
            return;
        if (gesture_.drag(working_, curveArea(), {e.position.x, e.position.y}, e.isShiftDown())) {
            context_.model.setCurve(lane_, working_);
            redraw();
        }
    }

    void mouseUp(const visage::MouseEvent& e) override {
        if (!gesture_.active())
            return;
        if (gesture_.release(working_, curveArea(), e.isShiftDown())) {
            context_.model.setCurve(lane_, working_);
            redraw();
        }
    }

    void draw(visage::Canvas& canvas) override {
        drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);
        drawLabel(canvas, context_, lane_ == 0 ? "LANE 1" : "LANE 2", context_.px(10.0f), context_.px(7.0f),
                  context_.px(52.0f), context_.px(20.0f), accent_, visage::Font::kLeft, 10.2f, Weight::semibold);

        const auto area = curveArea();
        canvas.setColor(0xff080b10u);
        canvas.roundedRectangle(area.x, area.y, area.width, area.height, context_.px(metric::radiusSmall));
        canvas.setColor(colour::edge);
        canvas.roundedRectangleBorder(area.x, area.y, area.width, area.height, context_.px(metric::radiusSmall),
                                      context_.px(1.0f));

        // 4-division beat grid + center zero line
        for (int i = 1; i < 4; ++i) {
            const float gx = area.x + area.width * (static_cast<float>(i) * 0.25f);
            canvas.setColor(colour::withAlpha(colour::text, 0.07f));
            canvas.rectangle(gx, area.y, context_.px(1.0f), area.height);
        }
        canvas.setColor(colour::withAlpha(colour::text, 0.09f));
        canvas.rectangle(area.x, area.y + area.height * 0.5f, area.width, context_.px(1.0f));

        const hba::dsp::Curve curve = gesture_.active() ? working_ : context_.model.curve(lane_);
        std::array<float, 128> samples{};
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const double p = static_cast<double>(i) / static_cast<double>(samples.size() - 1);
            samples[i] = hba::dsp::evaluate(curve, p);
        }
        hba::ui::drawCurve(canvas, context_.scale, samples, area.x, area.y + context_.px(4.0f), area.width,
                           area.height - context_.px(8.0f), accent_, -1.0f, 1.0f, 0.16f, -1.0f, 1.8f);

        // Draw editable breakpoint handles (double-click adds/removes; drag segment bends tension!)
        for (int i = 0; i < curve.count; ++i) {
            const auto pos = hba::ui::edit::pointPos(area, curve.points[static_cast<std::size_t>(i)]);
            const bool moving = (gesture_.movingPoint() == i);
            const float d = context_.px(moving ? 9.0f : 6.5f);
            canvas.setColor(0xff080b10u);
            canvas.circle(pos.x - d * 0.5f - context_.px(1.0f), pos.y - d * 0.5f - context_.px(1.0f),
                          d + context_.px(2.0f));
            canvas.setColor(moving ? colour::text : accent_);
            canvas.circle(pos.x - d * 0.5f, pos.y - d * 0.5f, d);
        }

        // Live transport phase cursor
        const auto& tel = context_.model.visuals();
        const float phase = std::clamp(lane_ == 0 ? tel.lane1Phase : tel.lane2Phase, 0.0f, 1.0f);
        const float cx = area.x + phase * area.width;
        canvas.setColor(colour::withAlpha(colour::text, 0.55f));
        canvas.rectangle(cx - context_.px(0.6f), area.y, context_.px(1.2f), area.height);
    }

  private:
    hba::ui::edit::Area curveArea() const {
        const float pad = context_.px(10.0f);
        const float top = context_.px(34.0f);
        return {pad, top, std::max(40.0f, width() - 2.0f * pad), std::max(30.0f, height() - top - pad)};
    }

    Context& context_;
    int lane_;
    unsigned accent_;
    Chip rateChip_;
    Chip targetChip_;
    Row depthRow_;
    ActionButton btnUp_, btnDn_, btnTri_, btnBloom_, btnStep_;
    hba::ui::edit::Gesture gesture_;
    hba::dsp::Curve working_{};
};

class MovementPanel final : public Panel {
  public:
    explicit MovementPanel(Context& ctx)
        : Panel(ctx), lane1_(ctx, 0, colour::spiral), lane2_(ctx, 1, colour::copedent) {
        addChild(&lane1_);
        addChild(&lane2_);
    }

    void resized() override {
        const float gap = context_.px(metric::gap);
        const float halfW = (width() - gap) * 0.5f;
        lane1_.setBounds(0.0f, 0.0f, halfW, height());
        lane2_.setBounds(halfW + gap, 0.0f, halfW, height());
    }

  private:
    LaneCard lane1_;
    LaneCard lane2_;
};
} // namespace

std::unique_ptr<Panel> makeMovementPanel(Context& ctx) { return std::make_unique<MovementPanel>(ctx); }

} // namespace tg::ui
