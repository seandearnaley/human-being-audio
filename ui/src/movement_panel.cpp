// The four movement lanes. Each row reads left to right: on and name, rate,
// phase, the lane's shape over two bars with a shared playhead, shape, depth.
#include "panels.h"

#include "tf/lanes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace tf::ui {

namespace {
// Column widths in design pixels. The curve takes what is left.
constexpr float kNameWidth = 100.0f;
constexpr float kRateWidth = 64.0f;
constexpr float kPhaseWidth = 52.0f;
constexpr float kShapeWidth = 84.0f;
constexpr float kDepthWidth = 56.0f;
constexpr float kCurveHeight = 30.0f;
constexpr float kDot = 5.0f;
constexpr float kPlayhead = 1.5f;

// The curve always shows two bars of 4/4, so every lane shares one timeline.
constexpr double kWindowQuarters = 8.0;
constexpr int kQuartersPerBar = 4;
constexpr double kAlmostOne = 1.0 - 1.0e-9;

constexpr std::array<const char*, kLaneCount> kLaneNames { "Phase", "Tract", "Fold", "Detune" };

// Every lane parameter plus Motion: what the curves depend on.
constexpr std::size_t kWatched = static_cast<std::size_t>(kLaneCount) * 5 + 1;

bool laneOn(const Model& model, int lane) { return model.get(laneParam(lane, LaneField::on)) >= 0.5f; }

int choiceIndex(const Model& model, ParamId id) {
    const auto& s = tf::spec(id);
    return std::clamp(static_cast<int>(model.get(id) + 0.5f), 0, static_cast<int>(s.choices.size()) - 1);
}

// The lane's output at a host position, after depth and Motion: what the engine plays.
float laneValueAt(const Model& model, int lane, double ppq) {
    const double length = kLaneRateQuarters[static_cast<std::size_t>(choiceIndex(model, laneParam(lane, LaneField::rate)))];
    const auto shape = static_cast<LaneShape>(choiceIndex(model, laneParam(lane, LaneField::shape)));
    const float phase = tf::lanePhase(ppq, length, model.get(laneParam(lane, LaneField::phase)));
    return tf::laneShape(shape, phase) * model.get(laneParam(lane, LaneField::depth)) * model.get(ParamId::motion);
}

// The lane's name and its on switch: a toggle Row whose label carries the lane hue.
class LaneSwitch final : public ParamFrame {
public:
    LaneSwitch(Context& context, ParamId id, std::string name)
        : ParamFrame(context, id, colour::motion), name_(std::move(name)) {}

private:
    void paint(visage::Canvas& canvas) override {
        const float w = width();
        const float h = height();
        const bool on = value() >= 0.5f;
        drawLabel(canvas, context_, name_, 0.0f, 0.0f, w, h,
                  active() ? colour::text : on ? accent_ : colour::textFaint);

        // Same switch as Row draws for toggles.
        const float pillWidth = context_.px(24.0f);
        const float pillHeight = context_.px(13.0f);
        const float inset = context_.px(2.0f);
        const float x = w - pillWidth;
        const float y = (h - pillHeight) * 0.5f;
        canvas.setColor(on ? accent_ : colour::track);
        canvas.roundedRectangle(x, y, pillWidth, pillHeight, pillHeight * 0.5f);
        canvas.setColor(on ? colour::window : colour::textDim);
        const float dot = pillHeight - 2.0f * inset;
        canvas.circle(on ? x + pillWidth - inset - dot : x + inset, y + inset, dot);
    }

    std::string name_;
};

class MovementPanel final : public Panel {
public:
    explicit MovementPanel(Context& context) : Panel(context) {
        for (int lane = 0; lane < kLaneCount; ++lane) {
            auto& controls = lanes_[static_cast<std::size_t>(lane)];
            controls.on = &add<LaneSwitch>(context, laneParam(lane, LaneField::on),
                                           kLaneNames[static_cast<std::size_t>(lane)]);
            controls.rate = &add<Chip>(context, laneParam(lane, LaneField::rate), colour::textDim);
            controls.phase = &add<Chip>(context, laneParam(lane, LaneField::phase), colour::textDim);
            controls.shape = &add<Chip>(context, laneParam(lane, LaneField::shape), colour::textDim);
            controls.depth = &add<Chip>(context, laneParam(lane, LaneField::depth), colour::textDim);
        }
    }

    void resized() override {
        const Columns c = columns();
        const float rowHeight = px(metric::row);
        for (int lane = 0; lane < kLaneCount; ++lane) {
            const auto& controls = lanes_[static_cast<std::size_t>(lane)];
            const float y = snap(laneMid(lane) - rowHeight * 0.5f);
            const auto place = [&](visage::Frame& frame, float x, float right) {
                frame.setBounds(snap(x), y, snap(right) - snap(x), snap(y + rowHeight) - y);
            };
            place(*controls.on, c.name, c.name + px(kNameWidth));
            place(*controls.rate, c.rate, c.rate + px(kRateWidth));
            place(*controls.phase, c.phase, c.phase + px(kPhaseWidth));
            place(*controls.shape, c.shape, c.shape + px(kShapeWidth));
            place(*controls.depth, c.depth, c.depth + px(kDepthWidth));
        }
    }

    void tick() override {
        if (context_.model.visuals().ppq != drawnPpq_ || watched() != drawnState_) redraw();
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);

        drawnPpq_ = context_.model.visuals().ppq;
        drawnState_ = watched();

        const Columns c = columns();
        const float left = snap(c.curve);
        const float right = snap(c.curve + c.curveWidth);
        const float curveWidth = right - left;
        if (curveWidth <= 0.0f) return;

        const double windowStart = std::floor(drawnPpq_ / kWindowQuarters) * kWindowQuarters;
        const auto playheadFraction = static_cast<float>((drawnPpq_ - windowStart) / kWindowQuarters);
        const float playheadX = left + curveWidth * playheadFraction;

        // One sample per pixel keeps square and step edges upright.
        const auto samples = static_cast<std::size_t>(std::max(2.0f, std::ceil(curveWidth) + 1.0f));
        values_.resize(samples);

        for (int lane = 0; lane < kLaneCount; ++lane) {
            const bool on = laneOn(context_.model, lane);
            const unsigned hue = on ? colour::motion : colour::textFaint;
            const float top = snap(laneMid(lane) - px(kCurveHeight) * 0.5f);
            const float height = snap(laneMid(lane) + px(kCurveHeight) * 0.5f) - top;

            drawGrid(canvas, left, curveWidth, top, height);

            const float hairline = std::max(1.0f, snap(px(metric::hairline)));
            canvas.setColor(colour::track);
            canvas.fill(left, snap(top + (height - hairline) * 0.5f), curveWidth, hairline);

            for (std::size_t i = 0; i < samples; ++i) {
                // The last sample stops just short of the window's end, so a ramp
                // or step that wraps there does not draw a jump on the edge.
                const double t = std::min(static_cast<double>(i) / static_cast<double>(samples - 1), kAlmostOne);
                values_[i] = laneValueAt(context_.model, lane, windowStart + t * kWindowQuarters);
            }
            // Inset by half a stroke so a full-depth lane is not clipped at its peaks.
            const float inset = px(metric::stroke) * 0.5f;
            drawCurve(canvas, context_, values_, left, top + inset, curveWidth, height - 2.0f * inset, hue, -1.0f,
                      1.0f, on ? 0.12f : 0.0f);

            canvas.setColor(colour::withAlpha(colour::text, 0.4f));
            canvas.fill(playheadX - px(kPlayhead) * 0.5f, top, px(kPlayhead), height);

            if (on) {
                const float value = laneValueAt(context_.model, lane, drawnPpq_);
                const float y = top + inset + (height - 2.0f * inset) * (1.0f - (value + 1.0f) * 0.5f);
                const float ring = px(kDot) + 2.0f * px(metric::stroke);
                canvas.setColor(colour::panel);
                canvas.circle(playheadX - ring * 0.5f, y - ring * 0.5f, ring);
                canvas.setColor(colour::motion);
                canvas.circle(playheadX - px(kDot) * 0.5f, y - px(kDot) * 0.5f, px(kDot));
            }
        }
    }

private:
    struct LaneControls {
        ParamFrame* on = nullptr;
        ParamFrame* rate = nullptr;
        ParamFrame* phase = nullptr;
        ParamFrame* shape = nullptr;
        ParamFrame* depth = nullptr;
    };

    // Left edges in frame pixels, identical for every lane so the columns line up.
    struct Columns {
        float name, rate, phase, curve, curveWidth, shape, depth;
    };

    Columns columns() const {
        const float gap = px(metric::gap);
        Columns c {};
        c.name = px(metric::pad);
        c.rate = c.name + px(kNameWidth) + gap;
        c.phase = c.rate + px(kRateWidth) + gap;
        c.curve = c.phase + px(kPhaseWidth) + gap;
        c.depth = width() - px(metric::pad) - px(kDepthWidth);
        c.shape = c.depth - gap - px(kShapeWidth);
        c.curveWidth = c.shape - gap - c.curve;
        return c;
    }

    // Lanes split the padded panel height equally.
    float laneMid(int lane) const {
        const float laneHeight = (height() - 2.0f * px(metric::pad)) / static_cast<float>(kLaneCount);
        return px(metric::pad) + laneHeight * (static_cast<float>(lane) + 0.5f);
    }

    static float snap(float v) { return std::round(v); }

    // A hairline on every quarter note, a little stronger on the bar line.
    void drawGrid(visage::Canvas& canvas, float left, float curveWidth, float top, float height) const {
        const float hairline = std::max(1.0f, snap(px(metric::hairline)));
        const int quarters = static_cast<int>(kWindowQuarters);
        for (int q = 1; q < quarters; ++q) {
            const float x = snap(left + curveWidth * static_cast<float>(q) / static_cast<float>(quarters));
            canvas.setColor(colour::withAlpha(colour::text, q % kQuartersPerBar == 0 ? 0.10f : 0.04f));
            canvas.fill(x, top, hairline, height);
        }
    }

    std::array<float, kWatched> watched() const {
        std::array<float, kWatched> state {};
        for (std::size_t i = 0; i + 1 < kWatched; ++i)
            state[i] = context_.model.get(static_cast<ParamId>(static_cast<std::size_t>(ParamId::lane1On) + i));
        state[kWatched - 1] = context_.model.get(ParamId::motion);
        return state;
    }

    std::array<LaneControls, kLaneCount> lanes_ {};
    std::vector<float> values_;
    double drawnPpq_ = -1.0;
    std::array<float, kWatched> drawnState_ {};
};
}

std::unique_ptr<Panel> makeMovementPanel(Context& context) { return std::make_unique<MovementPanel>(context); }

}
