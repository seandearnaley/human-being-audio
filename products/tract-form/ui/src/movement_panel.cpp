// The four movement lanes. Each reads left to right: on and target, source,
// rate (or ratio to the leader), phase, the lane's output over two bars with a
// shared playhead, shape, depth. One lane is selected and opens an editor for
// its curve under that line; the others stay one line high.
#include "panels.h"

#include "movement_edit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace tf::ui {

namespace {
// Heights in design pixels. Three compact lanes and the selected one fill the
// panel's 152 px of content exactly.
constexpr float kLine = 22.0f;          // one lane's line of controls
constexpr float kChipHeight = 18.0f;    // chips and the compact curve, centred on the line
constexpr float kSelected = 86.0f;      // the selected lane: its line, then the editor
constexpr float kEditorGap = 4.0f;      // line to editor
constexpr float kEditorBelow = 2.0f;    // editor to the next lane's line
static_assert(kSelected + (kLaneCount - 1) * kLine == 176.0f - 2.0f * metric::pad, "the lanes fill the panel");

// Column widths in design pixels. The curve takes what is left.
constexpr float kSwitchWidth = 24.0f;
constexpr float kTargetWidth = 76.0f;
constexpr float kFollowWidth = 54.0f;
constexpr float kRateWidth = 56.0f;
constexpr float kModeWidth = 54.0f;
constexpr float kPhaseWidth = 50.0f;
constexpr float kShapeWidth = 80.0f;
constexpr float kDepthWidth = 54.0f;
constexpr float kPlotInset = 8.0f;      // curve column edge to the plotted curve, in strips and editor
constexpr float kEditorInsetY = 7.0f;   // editor edge to its ±1 lines

constexpr float kDot = 5.0f;            // the playhead's dot on each strip
constexpr float kPlayhead = 1.5f;
constexpr float kHandle = 6.0f;         // a breakpoint's diameter
constexpr float kHandleActive = 8.0f;   // hovered or being dragged
constexpr float kHandleReach = 8.0f;    // how far from a handle a press still picks it
constexpr float kLineReach = 5.0f;      // how far from a line a press bends it
constexpr float kSlop = 3.0f;

constexpr float kFillAlpha = 0.12f;
constexpr float kFollowerAlpha = 0.6f;  // a following lane draws its leader's shape, dimmer
constexpr float kLeaderAlpha = 0.45f;   // the leader's curve, read-only in a follower's editor

// The strip always shows two bars of 4/4, so every lane shares one timeline.
constexpr double kWindowQuarters = 8.0;
constexpr int kQuartersPerBar = 4;
constexpr double kAlmostOne = 1.0 - 1.0e-6;

float snap(float v) { return std::round(v); }

ParamId field(int lane, LaneField f) { return laneParam(lane, f); }

LaneShape shapeOf(const Params& params, int lane) {
    return static_cast<LaneShape>(std::clamp(params.choice(field(lane, LaneField::shape)), 0,
                                             static_cast<int>(LaneShape::custom)));
}

bool following(const Params& params, int lane) { return laneSource(params, lane) != lane; }

// The curve a lane's cycle plays: its own, or its leader's. A built-in shape
// shows as the curve it would become if edited, which plays identically.
Curve cycleCurve(const Model& model, const Params& params, int lane) {
    const int source = laneSource(params, lane);
    const LaneShape shape = shapeOf(params, source);
    return shape == LaneShape::custom ? model.laneCurve(source) : startingCurve(shape, source);
}

// A hairline in whole device pixels.
float hairline(const Context& context) { return std::max(1.0f, snap(context.px(metric::hairline))); }

// The lane's on switch, drawn as the switch a Row draws for toggles.
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

// Which lane this one follows. A lane cannot follow itself, so its own number
// reads as Own and is left out of the menu.
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

// One cycle of the selected lane's curve, edited in place. A following lane has
// no curve of its own, so it shows its leader's, read-only.
class CurveEditor final : public visage::Frame {
public:
    explicit CurveEditor(Context& context) : context_(context) {}

    std::function<void()> onEdit;

    void setLane(int lane) {
        if (lane == lane_) return;
        lane_ = lane;
        hover_ = -1;
        gesture_ = {};
        redraw();
    }

    void draw(visage::Canvas& canvas) override {
        drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::raised, metric::radiusSmall);
        const Params params = paramsOf(context_.model);
        const edit::Area plot = area();
        drawGrid(canvas, plot);

        const bool on = params.on(field(lane_, LaneField::on));
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
            const auto& v = context_.model.visuals();
            const auto phase = static_cast<float>(lanePhaseFor(params, lane_, v.ppq, v.notePpq));
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

    bool editable() const { return !following(paramsOf(context_.model), lane_); }

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

    // The curve first, then the shape, so the lane never plays a stale curve.
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
            if (option == kRemove)
                removePoint(edited, index);
            else
                edited.points[static_cast<std::size_t>(index)].segment = static_cast<Segment>(option);
            hover_ = -1;
            commit(edited);
        };
        menu.show(this);
    }

    // Sixteenths of the cycle, where points snap, a little stronger on the quarters.
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
            c.mode = &add<Chip>(context, field(lane, LaneField::mode), colour::textDim);
            c.phase = &add<Chip>(context, field(lane, LaneField::phase), colour::textDim);
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
        const auto& v = context_.model.visuals();
        if (changed || v.ppq != drawnPpq_ || v.notePpq != drawnNotePpq_) {
            redraw();
            editor_->redraw();
        }
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const auto& v = context_.model.visuals();
        drawnPpq_ = v.ppq;
        drawnNotePpq_ = v.notePpq;

        const Params params = paramsOf(context_.model);
        const LaneCurves curves = curvesOf(context_.model);
        const Columns c = columns();
        const float left = snap(c.curve + px(kPlotInset));
        const float plotWidth = snap(c.curve + c.curveWidth - px(kPlotInset)) - left;
        if (plotWidth <= 0.0f) return;

        const double windowStart = std::floor(drawnPpq_ / kWindowQuarters) * kWindowQuarters;
        const float syncPlayheadX = left + plotWidth * static_cast<float>((drawnPpq_ - windowStart) / kWindowQuarters);
        const float motion = params[ParamId::motion];

        // One sample per pixel keeps square and step edges upright.
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
            const LaneMode mode = laneModeOf(params, lane);
            const bool noteClock = mode != LaneMode::sync;
            const double noteWindow =
                mode == LaneMode::oneShot ? 0.0 : std::floor(drawnNotePpq_ / kWindowQuarters) * kWindowQuarters;
            const float playheadX =
                noteClock ? left + plotWidth * static_cast<float>(
                                                   std::clamp((drawnNotePpq_ - noteWindow) / kWindowQuarters, 0.0, 1.0))
                          : syncPlayheadX;
            for (std::size_t i = 0; i < values_.size(); ++i) {
                // The last sample stops just short of the window's end, so a ramp
                // or step that wraps there does not draw a jump on the edge.
                const double t = std::min(static_cast<double>(i) / static_cast<double>(values_.size() - 1), kAlmostOne);
                const double samplePpq = windowStart + t * kWindowQuarters;
                const double sampleNotePpq = noteWindow + t * kWindowQuarters;
                values_[i] = static_cast<float>(laneOutput(params, curves, lane, samplePpq, sampleNotePpq)) * scale;
            }
            const unsigned base = on ? colour::motion : colour::textFaint;
            const unsigned hue = following(params, lane) ? colour::withAlpha(base, kFollowerAlpha) : base;
            // Inset by half a stroke so a full-depth lane is not clipped at its peaks.
            const float inset = px(metric::stroke) * 0.5f;
            drawCurve(canvas, context_, values_, left, top + inset, plotWidth, height - 2.0f * inset, hue, -1.0f, 1.0f,
                      on ? kFillAlpha : 0.0f);

            canvas.setColor(colour::withAlpha(colour::text, 0.4f));
            canvas.fill(playheadX - px(kPlayhead) * 0.5f, top, px(kPlayhead), height);

            if (on) {
                const float value =
                    static_cast<float>(laneOutput(params, curves, lane, drawnPpq_, drawnNotePpq_)) * scale;
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
        Chip* mode = nullptr;
        Chip* phase = nullptr;
        Chip* shape = nullptr;
        Chip* depth = nullptr;
    };

    // Left edges in frame pixels, identical for every lane so the columns line up.
    struct Columns {
        float on, target, follow, rate, mode, phase, curve, curveWidth, shape, depth;
    };

    // Every lane parameter, Motion, the curves and the selection: what drawing depends on.
    struct State {
        std::array<float, kLaneCount * kLaneFieldCount + 1> params {};
        LaneCurves curves {};
        bool operator==(const State&) const = default;
    };

    State watch() const {
        State state;
        std::size_t idx = 0;
        for (int lane = 0; lane < kLaneCount; ++lane)
            for (int f = 0; f < kLaneFieldCount; ++f)
                state.params[idx++] = context_.model.get(laneParam(lane, static_cast<LaneField>(f)));
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
        c.mode = c.rate + px(kRateWidth) + gap;
        c.phase = c.mode + px(kModeWidth) + gap;
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
            place(*controls.mode, c.mode, px(kModeWidth), chip);
            place(*controls.phase, c.phase, px(kPhaseWidth), chip);
            place(*controls.shape, c.shape, px(kShapeWidth), chip);
            place(*controls.depth, c.depth, px(kDepthWidth), chip);
        }
        const float top = snap(lineTop(selected_) + px(kLine + kEditorGap));
        const float bottom = snap(lineTop(selected_) + px(kSelected - kEditorBelow));
        editor_->setBounds(snap(c.curve), top, snap(c.curve + c.curveWidth) - snap(c.curve), bottom - top);
    }

    // Following swaps rate for ratio and hides the shape; an off lane reads faint.
    void updateControls() {
        const Params params = paramsOf(context_.model);
        for (int lane = 0; lane < kLaneCount; ++lane) {
            auto& controls = lanes_[static_cast<std::size_t>(lane)];
            const bool follows = following(params, lane);
            controls.rate->setVisible(!follows);
            controls.ratio->setVisible(follows);
            controls.shape->setVisible(!follows);

            const bool on = params.on(field(lane, LaneField::on));
            const unsigned dim = on ? colour::textDim : colour::textFaint;
            controls.target->setAccent(on ? colour::motion : colour::textFaint);
            controls.follow->setAccent(dim);
            const bool noteClock = laneModeOf(params, lane) != LaneMode::sync;
            controls.mode->setAccent(on && noteClock ? colour::master : dim);
            for (Chip* chip : { controls.rate, controls.ratio, controls.phase, controls.shape, controls.depth })
                chip->setAccent(dim);
        }
    }

    // The compact lane whose curve strip is under the mouse, or -1.
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

    // A hairline on every quarter note, a little stronger on the bar line.
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
    double drawnNotePpq_ = -1.0;
    State shown_ {};
};
}

std::unique_ptr<Panel> makeMovementPanel(Context& context) { return std::make_unique<MovementPanel>(context); }

}
