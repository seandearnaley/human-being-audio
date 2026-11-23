#include "panels.h"

#include "movement_edit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace pf::ui {

namespace {
constexpr float kLine = 22.0f;
constexpr float kChipHeight = 18.0f;
constexpr float kSelected = 86.0f;
constexpr float kEditorGap = 4.0f;
constexpr float kEditorBelow = 2.0f;
static_assert(kSelected + (kLaneCount - 1) * kLine == 176.0f - 2.0f * metric::pad, "the lanes fill the panel");

constexpr float kSwitchWidth = 24.0f;
constexpr float kTargetWidth = 76.0f;
constexpr float kFollowWidth = 56.0f;
constexpr float kRateWidth = 60.0f;
constexpr float kPhaseWidth = 52.0f;
constexpr float kShapeWidth = 84.0f;
constexpr float kDepthWidth = 56.0f;
constexpr float kPlotInset = 8.0f;
constexpr float kEditorInsetY = 7.0f;

constexpr float kDot = 5.0f;
constexpr float kPlayhead = 1.5f;
constexpr float kHandle = 6.0f;
constexpr float kHandleActive = 8.0f;
constexpr float kHandleReach = 8.0f;
constexpr float kLineReach = 5.0f;
constexpr float kSlop = 3.0f;

constexpr float kFillAlpha = 0.12f;
constexpr float kFollowerAlpha = 0.6f;
constexpr float kLeaderAlpha = 0.45f;

constexpr double kWindowQuarters = 8.0;
constexpr int kQuartersPerBar = 4;
constexpr double kAlmostOne = 1.0 - 1.0e-6;

float snap(float v) { return std::round(v); }
ParamId field(int lane, LaneField f) { return laneParam(lane, f); }

LaneShape shapeOf(const Params& params, int lane) {
    return static_cast<LaneShape>(
        std::clamp(params.choice(field(lane, LaneField::shape)), 0, static_cast<int>(LaneShape::env)));
}

bool following(const Params& params, int lane) { return laneSource(params, lane) != lane; }
bool isEnvLane(const Params& params, int lane) { return laneEffectiveShape(params, lane) == LaneShape::env; }

Curve cycleCurve(const Model& model, const Params& params, int lane) {
    const int source = laneSource(params, lane);
    const LaneShape shape = shapeOf(params, source);
    return shape == LaneShape::custom ? model.laneCurve(source) : startingCurve(shape, source);
}

float hairline(const Context& context) { return std::max(1.0f, snap(context.px(metric::hairline))); }

class LaneSwitch final : public ParamFrame {
public:
    LaneSwitch(Context& context, ParamId id) : ParamFrame(context, id, colour::motion) {}

private:
    void paint(visage::Canvas& canvas) override {
        const float pillWidth = context_.px(24.0f);
        const float pillHeight = context_.px(13.0f);
        const float inset = context_.px(2.0f);
        const float x = width() - pillWidth;
        const float y = (height() - pillHeight) * 0.5f;
        const bool on = value() >= 0.5f;
        canvas.setColor(on ? accent_ : active() ? colour::textDim : colour::track);
        canvas.roundedRectangle(x, y, pillWidth, pillHeight, pillHeight * 0.5f);
        canvas.setColor(on ? colour::window : colour::textDim);
        const float dot = pillHeight - 2.0f * inset;
        canvas.circle(on ? x + pillWidth - inset - dot : x + inset, y + inset, dot);
    }
};

class FollowChip final : public ParamFrame {
public:
    FollowChip(Context& context, int lane)
        : ParamFrame(context, field(lane, LaneField::follow), colour::textDim), lane_(lane) {}

    void setAccent(unsigned accent) {
        if (accent == accent_) return;
        accent_ = accent;
        redraw();
    }

    void mouseDown(const visage::MouseEvent& e) override {
        if (e.repeatClickCount() >= 2) return ParamFrame::mouseDown(e);
        visage::PopupMenu menu;
        for (std::size_t i = 0; i < spec().choices.size(); ++i)
            if (static_cast<int>(i) != lane_ + 1) menu.addOption(static_cast<int>(i), std::string(spec().choices[i]));
        menu.onSelection() = [this](int index) {
            context_.model.begin(id());
            context_.model.set(id(), static_cast<float>(index));
            context_.model.end(id());
            redraw();
        };
        menu.show(this);
    }

private:
    void paint(visage::Canvas& canvas) override {
        const int choice = static_cast<int>(value() + 0.5f);
        const std::string label(spec().choices[static_cast<std::size_t>(choice == lane_ + 1 ? 0 : choice)]);
        drawChip(canvas, context_, label, width(), height(), active() ? colour::text : accent_);
    }

    int lane_;
};

class CurveEditor final : public visage::Frame {
public:
  explicit CurveEditor(Context& context) : context_(context), envSrc_(context, ParamId::envSource, colour::sub) {
      addChild(&envSrc_);
      envSrc_.setVisible(false);
  }

    std::function<void()> onEdit;

    void setLane(int lane) {
        if (lane == lane_) return;
        lane_ = lane;
        hover_ = -1;
        gesture_ = {};
        syncVisibility();
        redraw();
    }

    void syncVisibility() {
        const Params params = paramsOf(context_.model);
        const bool envMode = isEnvLane(params, lane_);
        envSrc_.setVisible(envMode);
        if (envMode)
            layoutEnvControls();
    }

    void resized() override { layoutEnvControls(); }

    void draw(visage::Canvas& canvas) override {
        drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::raised, metric::radiusSmall);
        const Params params = paramsOf(context_.model);
        const edit::Area plot = area();
        drawGrid(canvas, plot);

        const bool on = params.on(field(lane_, LaneField::on));
        if (isEnvLane(params, lane_)) {
            const Visuals& vis = context_.model.visuals();
            const unsigned hue = on ? colour::sub : colour::textFaint;
            drawCurve(canvas, context_, vis.envHistory, plot.x, plot.y, plot.width, plot.height, hue, 0.0f, 1.0f,
                      on ? 0.18f : 0.0f, 0.0f);
            drawLabel(canvas, context_, "Envelope Follower", plot.x + context_.px(2.0f), context_.px(4.0f),
                      context_.px(140.0f), context_.px(16.0f), on ? colour::sub : colour::textFaint,
                      visage::Font::kLeft, type::small);
            return;
        }

        const bool editable = !following(params, lane_);
        const Curve curve = editable && gesture_.active() ? working_ : cycleCurve(context_.model, params, lane_);
        const unsigned hue = on ? colour::motion : colour::textFaint;

        values_.resize(static_cast<std::size_t>(std::max(2.0f, std::ceil(plot.width) + 1.0f)));
        for (std::size_t i = 0; i < values_.size(); ++i) {
            const double x = std::min(static_cast<double>(i) / static_cast<double>(values_.size() - 1), kAlmostOne);
            values_[i] = evaluate(curve, x);
        }
        drawCurve(canvas, context_, values_, plot.x, plot.y, plot.width, plot.height,
                  editable ? hue : colour::withAlpha(hue, kLeaderAlpha), -1.0f, 1.0f,
                  editable && on ? kFillAlpha : 0.0f);
        if (!editable) return;

        if (on) {
            const auto phase = static_cast<float>(lanePhase(context_.model.visuals().ppq, laneLengthQuarters(params, lane_),
                                                            params[field(lane_, LaneField::phase)]));
            const float x = snap(edit::toX(plot, phase));
            canvas.setColor(colour::withAlpha(colour::text, 0.25f));
            canvas.fill(x, snap(plot.y), hairline(context_), snap(plot.height));
        }

        const int lit = gesture_.movingPoint() >= 0 ? gesture_.movingPoint() : hover_;
        for (int i = 0; i < curve.count; ++i) {
            const edit::Pos p = edit::pointPos(plot, curve.points[static_cast<std::size_t>(i)]);
            const bool active = i == lit;
            const float diameter = context_.px(active ? kHandleActive : kHandle);
            const float ring = diameter + 2.0f * context_.px(metric::stroke);
            canvas.setColor(colour::raised);
            canvas.circle(p.x - ring * 0.5f, p.y - ring * 0.5f, ring);
            canvas.setColor(active ? colour::text : hue);
            canvas.circle(p.x - diameter * 0.5f, p.y - diameter * 0.5f, diameter);
        }
    }

    void mouseMove(const visage::MouseEvent& e) override { setHover(pick(e)); }
    void mouseExit(const visage::MouseEvent&) override { setHover(-1); }

    void mouseDown(const visage::MouseEvent& e) override {
        if (!editable()) return;
        if (e.shouldTriggerPopup()) return showMenu(pick(e));
        working_ = editableCurve();
        const edit::Reach reach { context_.px(kHandleReach), context_.px(kLineReach), context_.px(kSlop) };
        if (gesture_.press(working_, area(), at(e), e.repeatClickCount(), e.isAltDown(), reach)) commit(working_);
        hover_ = pick(e);
        redraw();
    }

    void mouseDrag(const visage::MouseEvent& e) override {
        if (!gesture_.active()) return;
        if (gesture_.drag(working_, area(), at(e), e.isAltDown())) commit(working_);
        redraw();
    }

    void mouseUp(const visage::MouseEvent& e) override {
        if (!gesture_.active()) return;
        if (gesture_.release(working_, area(), e.isAltDown())) commit(working_);
        hover_ = pick(e);
        redraw();
    }

private:
    edit::Area area() const {
        const float x = context_.px(kPlotInset);
        const float y = context_.px(kEditorInsetY);
        return { x, y, width() - 2.0f * x, height() - 2.0f * y };
    }
    static edit::Pos at(const visage::MouseEvent& e) { return { e.position.x, e.position.y }; }
    bool editable() const {
        const Params params = paramsOf(context_.model);
        return !following(params, lane_) && !isEnvLane(params, lane_);
    }
    void layoutEnvControls() {
        const float w = context_.px(72.0f);
        const float h = context_.px(18.0f);
        const float inset = context_.px(4.0f);
        envSrc_.setBounds(snap(width() - inset - w), snap(inset), snap(w), snap(h));
    }
    Curve editableCurve() const { return cycleCurve(context_.model, paramsOf(context_.model), lane_); }
    int pick(const visage::MouseEvent& e) const {
        if (!editable()) return -1;
        return edit::pickPoint(editableCurve(), area(), at(e), context_.px(kHandleReach));
    }
    void setHover(int index) {
        if (index == hover_) return;
        hover_ = index;
        redraw();
    }
    void commit(const Curve& curve) {
        context_.model.setLaneCurve(lane_, curve);
        const ParamId shape = field(lane_, LaneField::shape);
        if (static_cast<LaneShape>(static_cast<int>(context_.model.get(shape) + 0.5f)) != LaneShape::custom) {
            context_.model.begin(shape);
            context_.model.set(shape, static_cast<float>(LaneShape::custom));
            context_.model.end(shape);
        }
        redraw();
        if (onEdit) onEdit();
    }
    void showMenu(int index) {
        if (index < 0) return;
        const Curve curve = editableCurve();
        const Segment current = curve.points[static_cast<std::size_t>(index)].segment;
        constexpr int kRemove = 3;
        visage::PopupMenu menu;
        menu.addOption(static_cast<int>(Segment::line), "Line").select(current == Segment::line);
        menu.addOption(static_cast<int>(Segment::smooth), "Smooth").select(current == Segment::smooth);
        menu.addOption(static_cast<int>(Segment::hold), "Hold").select(current == Segment::hold);
        menu.addOption(kRemove, "Remove").enable(curve.count > 1);
        const int lane = lane_;
        menu.onSelection() = [this, lane, index](int option) {
            if (lane != lane_) return;
            Curve edited = editableCurve();
            if (index >= edited.count) return;
            if (option == kRemove) removePoint(edited, index);
            else edited.points[static_cast<std::size_t>(index)].segment = static_cast<Segment>(option);
            hover_ = -1;
            commit(edited);
        };
        menu.show(this);
    }
    void drawGrid(visage::Canvas& canvas, const edit::Area& plot) const {
        const float line = hairline(context_);
        for (int i = 1; i < edit::kSnapDivisions; ++i) {
            const float x = snap(edit::toX(plot, static_cast<float>(i) / edit::kSnapDivisions));
            canvas.setColor(colour::withAlpha(colour::text, i % 4 == 0 ? 0.08f : 0.03f));
            canvas.fill(x, snap(plot.y), line, snap(plot.height));
        }
        canvas.setColor(colour::track);
        canvas.fill(snap(plot.x), snap(plot.y + (plot.height - line) * 0.5f), snap(plot.width), line);
    }

    Context& context_;
    Chip envSrc_;
    int lane_ = 0;
    int hover_ = -1;
    edit::Gesture gesture_;
    Curve working_;
    std::vector<float> values_;
};

class MovementPanel final : public Panel {
public:
    explicit MovementPanel(Context& context) : Panel(context) {
        for (int lane = 0; lane < kLaneCount; ++lane) {
            auto& c = lanes_[static_cast<std::size_t>(lane)];
            c.on = &add<LaneSwitch>(context, field(lane, LaneField::on));
            c.target = &add<Chip>(context, field(lane, LaneField::target), colour::motion);
            c.follow = &add<FollowChip>(context, lane);
            c.rate = &add<Chip>(context, field(lane, LaneField::rate), colour::textDim);
            c.ratio = &add<Chip>(context, field(lane, LaneField::ratio), colour::textDim);
            c.phase = &add<Chip>(context, field(lane, LaneField::phase), colour::textDim);
            c.envAtk = &add<Chip>(context, ParamId::envAttack, colour::sub);
            c.envRel = &add<Chip>(context, ParamId::envRelease, colour::sub);
            c.shape = &add<Chip>(context, field(lane, LaneField::shape), colour::textDim);
            c.depth = &add<Chip>(context, field(lane, LaneField::depth), colour::textDim);
        }
        editor_ = &add<CurveEditor>(context);
        editor_->onEdit = [this] { redraw(); };
        updateControls();
    }

    void resized() override { layout(); }

    void tick() override {
        const State state = watch();
        const bool changed = state != shown_;
        if (changed) {
            shown_ = state;
            updateControls();
        }
        const Visuals& vis = context_.model.visuals();
        const bool envChanged =
            std::abs(vis.envLevel - drawnEnv_) > 1.0e-3f || std::abs(vis.envHistory.back() - drawnEnvTail_) > 1.0e-3f;
        if (changed || vis.ppq != drawnPpq_ || envChanged) {
            drawnEnv_ = vis.envLevel;
            drawnEnvTail_ = vis.envHistory.back();
            redraw();
            editor_->redraw();
        }
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const Visuals& vis = context_.model.visuals();
        drawnPpq_ = vis.ppq;
        drawnEnv_ = vis.envLevel;
        drawnEnvTail_ = vis.envHistory.back();

        const Params params = paramsOf(context_.model);
        const LaneCurves curves = curvesOf(context_.model);
        const Columns c = columns();
        const float left = snap(c.curve + px(kPlotInset));
        const float plotWidth = snap(c.curve + c.curveWidth - px(kPlotInset)) - left;
        if (plotWidth <= 0.0f) return;

        const double windowStart = std::floor(drawnPpq_ / kWindowQuarters) * kWindowQuarters;
        const float playheadX = left + plotWidth * static_cast<float>((drawnPpq_ - windowStart) / kWindowQuarters);
        const float motion = params[ParamId::motion];

        values_.resize(static_cast<std::size_t>(std::max(2.0f, std::ceil(plotWidth) + 1.0f)));

        for (int lane = 0; lane < kLaneCount; ++lane) {
            const float top = snap(lineTop(lane) + px((kLine - kChipHeight) * 0.5f));
            const float height = snap(lineTop(lane) + px((kLine + kChipHeight) * 0.5f)) - top;

            if (lane == hovered_ && lane != selected_)
                drawPanel(canvas, context_, snap(c.curve), top, snap(c.curve + c.curveWidth) - snap(c.curve), height,
                          colour::raised, metric::radiusSmall);
            drawGrid(canvas, left, plotWidth, top, height);

            const bool on = params.on(field(lane, LaneField::on));
            const float scale = params[field(lane, LaneField::depth)] * motion;
            const bool envMode = isEnvLane(params, lane);
            const float inset = px(metric::stroke) * 0.5f;

            if (envMode) {
                const unsigned base = on ? colour::sub : colour::textFaint;
                const unsigned hue = following(params, lane) ? colour::withAlpha(base, kFollowerAlpha) : base;
                std::array<float, Visuals::kEnvHistoryPoints> scaledEnv{};
                for (std::size_t i = 0; i < scaledEnv.size(); ++i)
                    scaledEnv[i] = std::clamp(vis.envHistory[i] * scale, -1.0f, 1.0f);
                drawCurve(canvas, context_, scaledEnv, left, top + inset, plotWidth, height - 2.0f * inset, hue, -1.0f,
                          1.0f, on ? 0.16f : 0.0f);
                if (on) {
                    const float val = std::clamp(vis.envLevel * scale, -1.0f, 1.0f);
                    const float y = top + inset + (height - 2.0f * inset) * (1.0f - (val + 1.0f) * 0.5f);
                    const float dotX = left + plotWidth;
                    const float ring = px(kDot) + 2.0f * px(metric::stroke);
                    canvas.setColor(lane == hovered_ && lane != selected_ ? colour::raised : colour::panel);
                    canvas.circle(dotX - ring * 0.5f, y - ring * 0.5f, ring);
                    canvas.setColor(hue);
                    canvas.circle(dotX - px(kDot) * 0.5f, y - px(kDot) * 0.5f, px(kDot));
                }
                continue;
            }

            for (std::size_t i = 0; i < values_.size(); ++i) {
                const double t = std::min(static_cast<double>(i) / static_cast<double>(values_.size() - 1), kAlmostOne);
                values_[i] = static_cast<float>(laneOutput(params, curves, lane, windowStart + t * kWindowQuarters)) * scale;
            }
            const unsigned base = on ? colour::motion : colour::textFaint;
            const unsigned hue = following(params, lane) ? colour::withAlpha(base, kFollowerAlpha) : base;
            drawCurve(canvas, context_, values_, left, top + inset, plotWidth, height - 2.0f * inset, hue, -1.0f, 1.0f,
                      on ? kFillAlpha : 0.0f);

            canvas.setColor(colour::withAlpha(colour::text, 0.4f));
            canvas.fill(playheadX - px(kPlayhead) * 0.5f, top, px(kPlayhead), height);

            if (on) {
                const float value = static_cast<float>(laneOutput(params, curves, lane, drawnPpq_)) * scale;
                const float y = top + inset + (height - 2.0f * inset) * (1.0f - (std::clamp(value, -1.0f, 1.0f) + 1.0f) * 0.5f);
                const float ring = px(kDot) + 2.0f * px(metric::stroke);
                canvas.setColor(lane == hovered_ && lane != selected_ ? colour::raised : colour::panel);
                canvas.circle(playheadX - ring * 0.5f, y - ring * 0.5f, ring);
                canvas.setColor(hue);
                canvas.circle(playheadX - px(kDot) * 0.5f, y - px(kDot) * 0.5f, px(kDot));
            }
        }
    }

    void mouseMove(const visage::MouseEvent& e) override { setHovered(stripAt(e)); }
    void mouseExit(const visage::MouseEvent&) override { setHovered(-1); }

    void mouseDown(const visage::MouseEvent& e) override {
        const int lane = stripAt(e);
        if (lane < 0 || lane == selected_) return;
        selected_ = lane;
        hovered_ = -1;
        editor_->setLane(lane);
        layout();
        redraw();
    }

private:
    struct LaneControls {
        LaneSwitch* on = nullptr;
        Chip* target = nullptr;
        FollowChip* follow = nullptr;
        Chip* rate = nullptr;
        Chip* ratio = nullptr;
        Chip* phase = nullptr;
        Chip* envAtk = nullptr;
        Chip* envRel = nullptr;
        Chip* shape = nullptr;
        Chip* depth = nullptr;
    };
    struct Columns {
        float on, target, follow, rate, phase, curve, curveWidth, shape, depth;
    };
    struct State {
        std::array<float, kLaneCount * kLaneFieldCount + 1> params {};
        LaneCurves curves {};
        bool operator==(const State&) const = default;
    };

    State watch() const {
        State state;
        for (std::size_t i = 0; i + 1 < state.params.size(); ++i)
            state.params[i] = context_.model.get(static_cast<ParamId>(static_cast<std::size_t>(ParamId::lane1On) + i));
        state.params.back() = context_.model.get(ParamId::motion);
        state.curves = curvesOf(context_.model);
        return state;
    }

    Columns columns() const {
        const float gap = px(metric::gap);
        Columns c {};
        c.on = px(metric::pad);
        c.target = c.on + px(kSwitchWidth) + gap;
        c.follow = c.target + px(kTargetWidth) + gap;
        c.rate = c.follow + px(kFollowWidth) + gap;
        c.phase = c.rate + px(kRateWidth) + gap;
        c.curve = c.phase + px(kPhaseWidth) + gap;
        c.depth = width() - px(metric::pad) - px(kDepthWidth);
        c.shape = c.depth - gap - px(kShapeWidth);
        c.curveWidth = c.shape - gap - c.curve;
        return c;
    }

    float lineTop(int lane) const {
        float top = metric::pad;
        for (int i = 0; i < lane; ++i) top += i == selected_ ? kSelected : kLine;
        return px(top);
    }

    void layout() {
        const Columns c = columns();
        for (int lane = 0; lane < kLaneCount; ++lane) {
            const auto& controls = lanes_[static_cast<std::size_t>(lane)];
            const float top = lineTop(lane);
            const auto place = [&](visage::Frame& frame, float x, float w, float h) {
                const float y = snap(top + (px(kLine) - h) * 0.5f);
                frame.setBounds(snap(x), y, snap(x + w) - snap(x), snap(y + h) - y);
            };
            const float chip = px(kChipHeight);
            place(*controls.on, c.on, px(kSwitchWidth), px(kLine));
            place(*controls.target, c.target, px(kTargetWidth), chip);
            place(*controls.follow, c.follow, px(kFollowWidth), chip);
            place(*controls.rate, c.rate, px(kRateWidth), chip);
            place(*controls.ratio, c.rate, px(kRateWidth), chip);
            place(*controls.phase, c.phase, px(kPhaseWidth), chip);
            place(*controls.envAtk, c.rate, px(kRateWidth), chip);
            place(*controls.envRel, c.phase, px(kPhaseWidth), chip);
            place(*controls.shape, c.shape, px(kShapeWidth), chip);
            place(*controls.depth, c.depth, px(kDepthWidth), chip);
        }
        const float top = snap(lineTop(selected_) + px(kLine + kEditorGap));
        const float bottom = snap(lineTop(selected_) + px(kSelected - kEditorBelow));
        editor_->setBounds(snap(c.curve), top, snap(c.curve + c.curveWidth) - snap(c.curve), bottom - top);
    }

    void updateControls() {
        const Params params = paramsOf(context_.model);
        for (int lane = 0; lane < kLaneCount; ++lane) {
            auto& controls = lanes_[static_cast<std::size_t>(lane)];
            const bool follows = following(params, lane);
            const bool envMode = isEnvLane(params, lane);
            controls.rate->setVisible(!follows && !envMode);
            controls.ratio->setVisible(follows && !envMode);
            controls.phase->setVisible(!envMode);
            controls.envAtk->setVisible(envMode && !follows);
            controls.envRel->setVisible(envMode && !follows);
            controls.shape->setVisible(!follows);

            const bool on = params.on(field(lane, LaneField::on));
            const unsigned dim = on ? colour::textDim : colour::textFaint;
            controls.target->setAccent(on ? (envMode ? colour::sub : colour::motion) : colour::textFaint);
            controls.follow->setAccent(dim);
            controls.envAtk->setAccent(on ? colour::sub : colour::textFaint);
            controls.envRel->setAccent(on ? colour::sub : colour::textFaint);
            for (Chip* chip : { controls.rate, controls.ratio, controls.phase, controls.shape, controls.depth })
                chip->setAccent(dim);
        }
        if (editor_ != nullptr)
            editor_->syncVisibility();
    }

    int stripAt(const visage::MouseEvent& e) const {
        const Columns c = columns();
        if (e.position.x < c.curve || e.position.x > c.curve + c.curveWidth) return -1;
        for (int lane = 0; lane < kLaneCount; ++lane) {
            const float top = lineTop(lane);
            if (e.position.y >= top && e.position.y < top + px(kLine)) return lane;
        }
        return -1;
    }

    void setHovered(int lane) {
        if (lane == selected_) lane = -1;
        if (lane == hovered_) return;
        hovered_ = lane;
        redraw();
    }

    void drawGrid(visage::Canvas& canvas, float left, float plotWidth, float top, float height) const {
        const float line = hairline(context_);
        const int quarters = static_cast<int>(kWindowQuarters);
        for (int q = 1; q < quarters; ++q) {
            const float x = snap(left + plotWidth * static_cast<float>(q) / static_cast<float>(quarters));
            canvas.setColor(colour::withAlpha(colour::text, q % kQuartersPerBar == 0 ? 0.10f : 0.04f));
            canvas.fill(x, top, line, height);
        }
        canvas.setColor(colour::track);
        canvas.fill(left, snap(top + (height - line) * 0.5f), plotWidth, line);
    }

    std::array<LaneControls, kLaneCount> lanes_ {};
    CurveEditor* editor_ = nullptr;
    std::vector<float> values_;
    int selected_ = 0;
    int hovered_ = -1;
    double drawnPpq_ = -1.0;
    float drawnEnv_ = -1.0f;
    float drawnEnvTail_ = -1.0f;
    State shown_ {};
};
}

std::unique_ptr<Panel> makeMovementPanel(Context& context) { return std::make_unique<MovementPanel>(context); }

}
