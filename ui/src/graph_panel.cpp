// The signal graph: Master, the two Reese voices with their feedback loop, Tract,
// Fold, Mid/side and Out, joined by wires, with the sub path underneath.
#include "panels.h"

#include "tf/feel.h"
#include "tf/vowels.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace tf::ui {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 2.0f * kPi;

// A rectangle in design pixels, relative to the panel.
struct Box {
    float x, y, w, h;
    constexpr float right() const { return x + w; }
    constexpr float bottom() const { return y + h; }
    constexpr float midX() const { return x + w * 0.5f; }
    constexpr float midY() const { return y + h * 0.5f; }
};

// Columns sit 28 apart; every node shares the same interior grid.
constexpr float kInset = 10.0f;         // node edge to content
constexpr float kTitleHeight = 16.0f;   // title band, from the top inset
constexpr float kVisualTop = 36.0f;     // from the node top
constexpr float kVisualHeight = 62.0f;
constexpr float kRowsTop = 110.0f;      // four rows end on the bottom inset
constexpr float kReeseRowsTop = 30.0f;  // the Reese cards' 2x2 grid
constexpr float kColumnGap = 20.0f;     // between the two columns of that grid
constexpr float kSparkWidth = 90.0f;

constexpr float kNodeTop = 12.0f;
constexpr float kNodeHeight = 208.0f;
constexpr Box kMaster { 12.0f, kNodeTop, 170.0f, kNodeHeight };
constexpr Box kReeseA { 210.0f, kNodeTop, 250.0f, 84.0f };
constexpr Box kReeseB { 210.0f, 104.0f, 250.0f, 84.0f };
constexpr Box kFeedback { 210.0f, 196.0f, 250.0f, 24.0f };
constexpr Box kTract { 488.0f, kNodeTop, 190.0f, kNodeHeight };
constexpr Box kFold { 706.0f, kNodeTop, 170.0f, kNodeHeight };
constexpr Box kMidSide { 904.0f, kNodeTop, 150.0f, kNodeHeight };
constexpr Box kOut { 1082.0f, kNodeTop, 162.0f, kNodeHeight };

constexpr float kSubY = 256.0f;
constexpr float kCorner = 10.0f;
// One line: title, Low-pass, Fit. Each row is as wide as its longest label and
// value ("200 Hz", "360°") plus the same 16 px the title leaves before Low-pass.
constexpr float kSubLockRowX = 76.0f;  // from the card's left edge
constexpr float kLowPassWidth = 108.0f;
constexpr float kFitWidth = 60.0f;
constexpr float kSubLockGap = 24.0f;   // between the two rows
constexpr float kSubLockWidth = kSubLockRowX + kLowPassWidth + kSubLockGap + kFitWidth + kInset;
constexpr Box kSubLock { (kMaster.midX() + kOut.midX() - kSubLockWidth) * 0.5f, kSubY - 18.0f, kSubLockWidth, 36.0f };

constexpr float kChipWidth = 52.0f;
constexpr float kTimeChipWidth = 64.0f;  // "1/4 cyc"
constexpr float kChipHeight = 20.0f;
constexpr float kChipGap = 6.0f;
constexpr float kFeedbackLabelWidth = 58.0f;  // "Feedback" at label size
constexpr float kLoopOut = 10.0f;             // the feedback loop's distance from the column

// Wires: alpha with the gate open and closed, and the flow of the travelling dots.
constexpr float kWireOpen = 0.85f;
constexpr float kWireClosed = 0.55f;
constexpr float kLoopAlpha = 0.6f;
constexpr float kFlowSpeed = 56.0f;    // design pixels per second: one column gap per 0.5 s
constexpr float kDotSpacing = 112.0f;  // design pixels between dots on a wire
constexpr float kDash = 4.0f;

constexpr float kLaneDepth = 0.5f;  // lanes 2 and 3 move morph and drive by up to this much

Box nodeTitle(const Box& node) { return { node.x + kInset, node.y + kInset, node.w - 2.0f * kInset, kTitleHeight }; }

Box nodeVisual(const Box& node, float width) {
    return { node.midX() - width * 0.5f, node.y + kVisualTop, width, kVisualHeight };
}

Box nodeRow(const Box& node, int index) {
    return { node.x + kInset, node.y + kRowsTop + metric::row * static_cast<float>(index), node.w - 2.0f * kInset,
             metric::row };
}

Box reeseCell(const Box& card, int column, int row) {
    const float width = (card.w - 2.0f * kInset - kColumnGap) * 0.5f;
    return { card.x + kInset + static_cast<float>(column) * (width + kColumnGap),
             card.y + kReeseRowsTop + metric::row * static_cast<float>(row), width, metric::row };
}

std::string noteName(int note) {
    static constexpr std::array<const char*, 12> kNames { "C", "C#", "D", "D#", "E", "F",
                                                          "F#", "G", "G#", "A", "A#", "B" };
    return std::string(kNames[static_cast<std::size_t>(note % 12)]) + std::to_string(note / 12 - 1);
}

std::string hzText(float hz) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, hz < 100.0f ? "%.2f Hz" : "%.1f Hz", static_cast<double>(hz));
    return buffer;
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------------------
// The shapes each node draws, computed from parameters.

template<std::size_t N>
using Curve = std::array<float, N>;

constexpr std::size_t kSparkPoints = 96;
constexpr std::size_t kShapePoints = 160;

Curve<kShapePoints> sinePeriod() {
    Curve<kShapePoints> values {};
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = std::sin(kTwoPi * static_cast<float>(i) / static_cast<float>(values.size() - 1));
    return values;
}

// One period of the master: sin(2π·x·ratio + phase + I·sin(2π·x)).
Curve<kSparkPoints> voicePeriod(float ratio, float phaseDegrees, float pmIndex) {
    Curve<kSparkPoints> values {};
    const float phase = phaseDegrees * kPi / 180.0f;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(values.size() - 1);
        values[i] = std::sin(kTwoPi * x * ratio + phase + pmIndex * std::sin(kTwoPi * x));
    }
    return values;
}

// Formant magnitude response, 150 Hz to 5 kHz on a log axis, in 0..1 over a
// 36 dB range below the highest peak.
Curve<kShapePoints> formantResponse(int vowelA, int vowelB, float morph) {
    constexpr float kLow = 150.0f, kHigh = 5000.0f, kRangeDb = 36.0f;
    const Vowel& a = kVowels[static_cast<std::size_t>(std::clamp(vowelA, 0, 5))];
    const Vowel& b = kVowels[static_cast<std::size_t>(std::clamp(vowelB, 0, 5))];

    std::array<float, kFormantCount> hz {}, bandwidth {}, gain {};
    for (std::size_t k = 0; k < kFormantCount; ++k) {
        hz[k] = std::exp(lerp(std::log(a.hz[k]), std::log(b.hz[k]), morph));
        bandwidth[k] = std::exp(lerp(std::log(a.bandwidthHz[k]), std::log(b.bandwidthHz[k]), morph));
        gain[k] = std::pow(10.0f, lerp(a.gainDb[k], b.gainDb[k], morph) / 20.0f);
    }

    Curve<kShapePoints> values {};
    float peak = -1000.0f;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float position = static_cast<float>(i) / static_cast<float>(values.size() - 1);
        const float f = kLow * std::pow(kHigh / kLow, position);
        float sum = 0.0f;
        for (std::size_t k = 0; k < kFormantCount; ++k) {
            const float q = hz[k] / bandwidth[k];
            const float detune = f / hz[k] - hz[k] / f;
            sum += gain[k] / std::sqrt(1.0f + q * q * detune * detune);
        }
        values[i] = 20.0f * std::log10(std::max(sum, 1.0e-6f));
        peak = std::max(peak, values[i]);
    }
    for (float& v : values) v = std::max(0.0f, 1.0f - (peak - v) / kRangeDb);
    return values;
}

// The folder's transfer curve over inputs -extent..extent.
Curve<kShapePoints> foldTransfer(FoldType type, float drive, float asym, float extent) {
    const float gain = feel::driveGain(drive);
    Curve<kShapePoints> values {};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float x = lerp(-extent, extent, static_cast<float>(i) / static_cast<float>(values.size() - 1));
        const float shaped = gain * x + 0.5f * asym;
        switch (type) {
            case FoldType::fold: {
                float m = std::fmod(shaped + 1.0f, 4.0f);
                if (m < 0.0f) m += 4.0f;
                values[i] = 1.0f - std::abs(m - 2.0f);
                break;
            }
            case FoldType::sine: values[i] = std::sin(0.5f * kPi * shaped); break;
            case FoldType::tube: values[i] = std::tanh(shaped); break;
        }
    }
    return values;
}

// ---------------------------------------------------------------------------
// Wires, sampled into polylines in design pixels so dots can travel along them.

struct Point {
    float x, y;
};

class Wire {
public:
    Wire(float x, float y) { points_.push_back({ x, y }); }

    Wire& to(float x, float y) {
        points_.push_back({ x, y });
        return *this;
    }

    // A bezier leaving and arriving horizontally.
    Wire& curveTo(float x, float y) {
        const Point from = points_.back();
        const float handle = (x - from.x) * 0.5f;
        return cubic({ from.x + handle, from.y }, { x - handle, y }, { x, y }, 32);
    }

    // A quarter-circle turn through the corner point `c`, ending at (x, y).
    Wire& turn(float cx, float cy, float x, float y) {
        constexpr float kKappa = 0.5523f;
        const Point from = points_.back();
        return cubic({ from.x + (cx - from.x) * kKappa, from.y + (cy - from.y) * kKappa },
                     { x + (cx - x) * kKappa, y + (cy - y) * kKappa }, { x, y }, 10);
    }

    const std::vector<Point>& points() const { return points_; }

    float length() const {
        float total = 0.0f;
        for (std::size_t i = 1; i < points_.size(); ++i) total += distance(points_[i - 1], points_[i]);
        return total;
    }

    Point at(float s) const {
        for (std::size_t i = 1; i < points_.size(); ++i) {
            const float segment = distance(points_[i - 1], points_[i]);
            if (s <= segment && segment > 0.0f) {
                const float t = s / segment;
                return { lerp(points_[i - 1].x, points_[i].x, t), lerp(points_[i - 1].y, points_[i].y, t) };
            }
            s -= segment;
        }
        return points_.back();
    }

private:
    static float distance(Point a, Point b) { return std::hypot(b.x - a.x, b.y - a.y); }

    Wire& cubic(Point c1, Point c2, Point end, int steps) {
        const Point start = points_.back();
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float u = 1.0f - t;
            const float a = u * u * u, b = 3.0f * u * u * t, c = 3.0f * u * t * t, d = t * t * t;
            points_.push_back({ a * start.x + b * c1.x + c * c2.x + d * end.x,
                                a * start.y + b * c1.y + c * c2.y + d * end.y });
        }
        return *this;
    }

    std::vector<Point> points_;
};

// Wire ends tuck this far under the node they meet, so caps never show.
constexpr float kTuck = 2.0f;

std::vector<std::pair<Wire, unsigned>> signalWires() {
    const auto link = [](const Box& from, float fromY, const Box& to, float toY) {
        Wire wire(from.right() - kTuck, fromY);
        wire.to(from.right(), fromY).curveTo(to.x, toY).to(to.x + kTuck, toY);
        return wire;
    };
    const float mid = kMaster.midY();
    std::vector<std::pair<Wire, unsigned>> wires;
    wires.emplace_back(link(kMaster, mid, kReeseA, kReeseA.midY()), colour::master);
    wires.emplace_back(link(kMaster, mid, kReeseB, kReeseB.midY()), colour::master);
    wires.emplace_back(link(kReeseA, kReeseA.midY(), kTract, mid), colour::growl);
    wires.emplace_back(link(kReeseB, kReeseB.midY(), kTract, mid), colour::growl);
    wires.emplace_back(link(kTract, mid, kFold, mid), colour::growl);
    wires.emplace_back(link(kFold, mid, kMidSide, mid), colour::growl);
    wires.emplace_back(link(kMidSide, mid, kOut, mid), colour::growl);

    // The sub: down from Master, along the lane, up into Out.
    const float left = kMaster.midX();
    const float right = kOut.midX();
    Wire sub(left, kMaster.bottom() - kTuck);
    sub.to(left, kSubY - kCorner)
        .turn(left, kSubY, left + kCorner, kSubY)
        .to(right - kCorner, kSubY)
        .turn(right, kSubY, right, kSubY - kCorner)
        .to(right, kOut.bottom() - kTuck);
    wires.emplace_back(std::move(sub), colour::master);
    return wires;
}

// Feedback: from Reese B's output, around under the column, back into its input.
Wire feedbackLoop() {
    const float y = kReeseB.midY();
    const float low = kFeedback.midY();
    const float right = kReeseB.right() + kLoopOut;
    const float left = kReeseB.x - kLoopOut;
    Wire loop(kReeseB.right() - kTuck, y);
    loop.to(kReeseB.right(), y)
        .turn(right, y, right, y + kCorner)
        .to(right, low - kCorner)
        .turn(right, low, right - kCorner, low)
        .to(left + kCorner, low)
        .turn(left, low, left, low - kCorner)
        .to(left, y + kCorner)
        .turn(left, y, left + kCorner, y)
        .to(kReeseB.x + kTuck, y);
    return loop;
}

// ---------------------------------------------------------------------------

// Where a movement lane has pushed a barred value: a violet span on the row's
// bar from the set value to the moved one, ending in a dot. Drawn over the row
// and transparent to the mouse.
class LaneMarker final : public visage::Frame {
public:
    static constexpr float kAbove = 6.0f;   // design pixels from the frame top to the row's bottom edge
    static constexpr float kHeight = 10.0f;

    LaneMarker(Context& context, ParamId id, int lane) : context_(context), id_(id), lane_(lane) {
        setIgnoresMouseEvents(true, false);
    }

    void draw(visage::Canvas& canvas) override {
        const float base = toNormalised(spec(id_), context_.model.get(id_));
        const float lane = context_.model.visuals().laneValue[static_cast<std::size_t>(lane_)];
        const float moved = std::clamp(base + kLaneDepth * lane, 0.0f, 1.0f);
        const float thickness = context_.px(2.0f);
        const float barY = context_.px(kAbove) - thickness;
        const float w = width();
        const float from = std::min(base, moved) * w;
        const float to = std::max(base, moved) * w;
        canvas.setColor(colour::motion);
        if (to - from > 0.5f) canvas.roundedRectangle(from, barY, to - from, thickness, thickness * 0.5f);
        const float dot = context_.px(5.0f);
        const float x = std::clamp(moved * w, dot * 0.5f, w - dot * 0.5f);
        canvas.circle(x - dot * 0.5f, barY + thickness * 0.5f - dot * 0.5f, dot);
    }

private:
    Context& context_;
    ParamId id_;
    int lane_;
};

class GraphPanel final : public Panel {
public:
    explicit GraphPanel(Context& context) : Panel(context) {
        // Master
        place<Row>(nodeRow(kMaster, 1), ParamId::glide, colour::master, "Glide", true);
        place<Row>(nodeRow(kMaster, 2), ParamId::phaseReset, colour::master, "Phase reset");
        place<Row>(nodeRow(kMaster, 3), ParamId::beatLock, colour::master, "Beat lock");

        // Reese A and B
        addVoice(kReeseA, ParamId::aDetune, ParamId::aRatio, ParamId::aPhase, ParamId::aPm);
        addVoice(kReeseB, ParamId::bDetune, ParamId::bRatio, ParamId::bPhase, ParamId::bPm);
        const float chipY = kFeedback.midY() - kChipHeight * 0.5f;
        const float amountX = kFeedback.right() - kInset - kChipWidth;
        place<Chip>({ amountX - kChipGap - kTimeChipWidth, chipY, kTimeChipWidth, kChipHeight }, ParamId::fbTime,
                    colour::growl);
        place<Chip>({ amountX, chipY, kChipWidth, kChipHeight }, ParamId::fbAmount, colour::growl);

        // Tract
        place<Row>(nodeRow(kTract, 0), ParamId::vowelA, colour::growl, "From");
        place<Row>(nodeRow(kTract, 1), ParamId::vowelB, colour::growl, "To");
        place<Row>(nodeRow(kTract, 2), ParamId::morph, colour::growl, "Morph", true);
        morphMarker_ = &place<LaneMarker>(markerBox(nodeRow(kTract, 2)), ParamId::morph, 1);

        // Fold
        place<Row>(nodeRow(kFold, 0), ParamId::foldType, colour::growl, "Type");
        place<Row>(nodeRow(kFold, 1), ParamId::drive, colour::growl, "Drive", true);
        place<Row>(nodeRow(kFold, 2), ParamId::asym, colour::growl, "Asym");
        place<Row>(nodeRow(kFold, 3), ParamId::weight, colour::growl, "Weight", true);
        driveMarker_ = &place<LaneMarker>(markerBox(nodeRow(kFold, 1)), ParamId::drive, 2);

        // Mid/side
        place<Row>(nodeRow(kMidSide, 0), ParamId::midGain, colour::text, "Mid");
        place<Row>(nodeRow(kMidSide, 1), ParamId::sideGain, colour::text, "Side");
        place<Row>(nodeRow(kMidSide, 2), ParamId::xover, colour::text, "Crossover");

        // Out
        place<Row>(nodeRow(kOut, 0), ParamId::growlLevel, colour::growl, "Growl", true);
        place<Row>(nodeRow(kOut, 1), ParamId::subLevel, colour::master, "Sub", true);

        // Sub lock
        const float subRowY = kSubLock.midY() - metric::row * 0.5f;
        const float subRowX = kSubLock.x + kSubLockRowX;
        place<Row>({ subRowX, subRowY, kLowPassWidth, metric::row }, ParamId::subFreq, colour::master, "Low-pass");
        place<Row>({ subRowX + kLowPassWidth + kSubLockGap, subRowY, kFitWidth, metric::row }, ParamId::fit,
                   colour::master, "Fit");

        lastTick_ = Clock::now();
    }

    void resized() override {
        for (const auto& [frame, box] : placed_)
            frame->setBounds(px(box.x), px(box.y), px(box.w), px(box.h));
    }

    void tick() override {
        const auto now = Clock::now();
        const float elapsed = std::min(std::chrono::duration<float>(now - lastTick_).count(), 0.1f);
        lastTick_ = now;

        const Watched watched = watch();
        const bool lanesMoved = watched.lanes != shown_.lanes;
        const bool changed = watched != shown_;
        shown_ = watched;

        if (watched.gate) flow_ = std::fmod(flow_ + elapsed * kFlowSpeed, kDotSpacing * 1000.0f);
        if (watched.gate || changed) redraw();
        if (lanesMoved || changed) {
            morphMarker_->redraw();
            driveMarker_->redraw();
        }
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const Visuals& visuals = context_.model.visuals();
        drawWires(canvas, visuals.gate);

        for (const Box& node : { kMaster, kReeseA, kReeseB, kTract, kFold, kMidSide, kOut, kSubLock })
            drawPanel(canvas, context_, px(node.x), px(node.y), px(node.w), px(node.h), colour::raised,
                      metric::radiusSmall);

        drawMaster(canvas, visuals);
        drawVoice(canvas, kReeseA, "Reese A", ParamId::aRatio, ParamId::aPhase, ParamId::aPm, visuals.laneValue[0]);
        drawVoice(canvas, kReeseB, "Reese B", ParamId::bRatio, ParamId::bPhase, ParamId::bPm, -visuals.laneValue[0]);
        drawFeedbackLabel(canvas);
        drawTract(canvas, visuals);
        drawFold(canvas, visuals);
        drawMidSide(canvas);
        drawOut(canvas, visuals);
        title(canvas, { kSubLock.x + kInset, kSubLock.midY() - kTitleHeight * 0.5f, kSubLockRowX, kTitleHeight },
              "Sub lock", colour::master);
    }

private:
    using Clock = std::chrono::steady_clock;

    // Everything the panel's own drawing depends on.
    struct Watched {
        std::array<float, 16> params {};
        std::array<float, 3> lanes {};
        float hz = 0.0f;
        int note = -1;
        bool gate = false;
        bool operator==(const Watched&) const = default;
    };

    static constexpr std::array<ParamId, 16> kWatchedParams {
        ParamId::coupling, ParamId::width,  ParamId::aRatio,   ParamId::aPhase,   ParamId::aPm,  ParamId::bRatio,
        ParamId::bPhase,   ParamId::bPm,    ParamId::vowelA,   ParamId::vowelB,   ParamId::morph, ParamId::foldType,
        ParamId::drive,    ParamId::asym,   ParamId::sideGain, ParamId::xover,
    };

    Watched watch() {
        Watched w;
        for (std::size_t i = 0; i < kWatchedParams.size(); ++i) w.params[i] = get(kWatchedParams[i]);
        const Visuals& visuals = context_.model.visuals();
        for (std::size_t i = 0; i < w.lanes.size(); ++i) w.lanes[i] = visuals.laneValue[i];
        w.hz = visuals.hz;
        w.note = visuals.note;
        w.gate = visuals.gate;
        return w;
    }

    float get(ParamId id) const { return context_.model.get(id); }

    template<typename T, typename... Args>
    T& place(const Box& box, Args&&... args) {
        T& frame = add<T>(context_, std::forward<Args>(args)...);
        placed_.push_back({ &frame, box });
        return frame;
    }

    static Box markerBox(const Box& row) {
        return { row.x, row.bottom() - LaneMarker::kAbove, row.w, LaneMarker::kHeight };
    }

    void addVoice(const Box& card, ParamId detune, ParamId ratio, ParamId phase, ParamId pm) {
        place<Row>(reeseCell(card, 0, 0), detune, colour::growl, "Detune");
        place<Row>(reeseCell(card, 1, 0), ratio, colour::growl, "Ratio");
        place<Row>(reeseCell(card, 0, 1), phase, colour::growl, "Phase");
        place<Row>(reeseCell(card, 1, 1), pm, colour::growl, "PM");
    }

    // --- drawing helpers, all in design pixels -----------------------------

    void title(visage::Canvas& canvas, const Box& box, const char* text, unsigned hue) {
        drawLabel(canvas, context_, text, px(box.x), px(box.y), px(box.w), px(box.h), hue, visage::Font::kLeft,
                  type::title, Weight::semibold);
    }

    template<std::size_t N>
    void curve(visage::Canvas& canvas, const Curve<N>& values, const Box& box, unsigned hue, float low, float high,
               float fillAlpha = 0.0f, float baseline = 0.0f) {
        drawCurve(canvas, context_, values, px(box.x), px(box.y), px(box.w), px(box.h), hue, low, high, fillAlpha,
                  baseline);
    }

    void hairline(visage::Canvas& canvas, Point a, Point b, unsigned hue) {
        visage::Path path;
        path.moveTo(px(a.x), px(a.y));
        path.lineTo(px(b.x), px(b.y));
        canvas.setColor(hue);
        canvas.stroke(path, 0.0f, 0.0f, width(), height(), px(metric::hairline));
    }

    void stroke(visage::Canvas& canvas, const Wire& wire, std::vector<float> dash = {}, float dashOffset = 0.0f) {
        visage::Path path;
        const auto& points = wire.points();
        path.moveTo(px(points.front().x), px(points.front().y));
        for (std::size_t i = 1; i < points.size(); ++i) path.lineTo(px(points[i].x), px(points[i].y));
        canvas.stroke(path, 0.0f, 0.0f, width(), height(), px(metric::stroke), visage::Path::Join::Round,
                      visage::Path::EndCap::Round, std::move(dash), dashOffset);
    }

    void drawWires(visage::Canvas& canvas, bool gate) {
        const float alpha = gate ? kWireOpen : kWireClosed;

        canvas.setColor(colour::withAlpha(colour::growl, alpha * kLoopAlpha));
        stroke(canvas, feedbackLoop(), { px(kDash), px(kDash) }, gate ? -px(flow_) : 0.0f);

        const auto wires = signalWires();
        for (const auto& [wire, hue] : wires) {
            canvas.setColor(colour::withAlpha(hue, alpha));
            stroke(canvas, wire);
        }
        if (!gate) return;

        // Dots ride the flow; their phase follows x so they read as one stream.
        const float dot = 3.5f;
        const float halo = 8.0f;
        for (const auto& [wire, hue] : wires) {
            const float length = wire.length();
            const float start = wire.points().front().x;
            float s = std::fmod(flow_ - start, kDotSpacing);
            if (s < 0.0f) s += kDotSpacing;
            for (; s < length; s += kDotSpacing) {
                const float fade = smoothstep(std::min(s, length - s) / 8.0f);
                const Point p = wire.at(s);
                canvas.setColor(colour::withAlpha(hue, 0.15f * fade));
                canvas.circle(px(p.x - halo * 0.5f), px(p.y - halo * 0.5f), px(halo));
                canvas.setColor(colour::withAlpha(hue, fade));
                canvas.circle(px(p.x - dot * 0.5f), px(p.y - dot * 0.5f), px(dot));
            }
        }
    }

    void drawMaster(visage::Canvas& canvas, const Visuals& visuals) {
        title(canvas, nodeTitle(kMaster), "Master", colour::master);
        static const auto sine = sinePeriod();
        curve(canvas, sine, nodeVisual(kMaster, kMaster.w - 2.0f * kInset), colour::master, -1.2f, 1.2f, 0.12f);

        const Box row = nodeRow(kMaster, 0);
        const bool sounding = visuals.note >= 0;
        drawLabel(canvas, context_, sounding ? noteName(visuals.note) : "–", px(row.x), px(row.y), px(row.w),
                  px(row.h), colour::textDim);
        if (sounding)
            drawLabel(canvas, context_, hzText(visuals.hz), px(row.x), px(row.y), px(row.w), px(row.h), colour::text,
                      visage::Font::kRight, type::label, Weight::regular);
    }

    void drawVoice(visage::Canvas& canvas, const Box& card, const char* name, ParamId ratio, ParamId phase, ParamId pm,
                   float lane) {
        const Box band = nodeTitle(card);
        title(canvas, band, name, colour::growl);
        const auto values = voicePeriod(get(ratio), get(phase) + 90.0f * lane,
                                        feel::pmIndex(get(pm), get(ParamId::coupling)));
        curve(canvas, values, { band.right() - kSparkWidth, band.y, kSparkWidth, band.h }, colour::growl, -1.15f, 1.15f);
    }

    void drawFeedbackLabel(visage::Canvas& canvas) {
        // The loop runs behind the strip; clear it where the label and chips sit.
        const float pad = 6.0f;
        const float labelX = kFeedback.x + kInset;
        const float chipsX = kFeedback.right() - kInset - kChipWidth - kChipGap - kTimeChipWidth;
        const float y = kFeedback.midY() - kChipHeight * 0.5f;
        canvas.setColor(colour::panel);
        canvas.rectangle(px(labelX - pad), px(y), px(kFeedbackLabelWidth + 2.0f * pad), px(kChipHeight));
        canvas.rectangle(px(chipsX - pad), px(y), px(kFeedback.right() - kInset - chipsX + 2.0f * pad),
                         px(kChipHeight));
        drawLabel(canvas, context_, "Feedback", px(labelX), px(kFeedback.y), px(kFeedbackLabelWidth),
                  px(kFeedback.h), colour::textDim);
    }

    void drawTract(visage::Canvas& canvas, const Visuals& visuals) {
        title(canvas, nodeTitle(kTract), "Tract", colour::growl);
        const float morph = std::clamp(get(ParamId::morph) + kLaneDepth * visuals.laneValue[1], 0.0f, 1.0f);
        const auto values = formantResponse(static_cast<int>(get(ParamId::vowelA) + 0.5f),
                                            static_cast<int>(get(ParamId::vowelB) + 0.5f), morph);
        curve(canvas, values, nodeVisual(kTract, kTract.w - 2.0f * kInset), colour::growl, 0.0f, 1.08f, 0.12f);
    }

    void drawFold(visage::Canvas& canvas, const Visuals& visuals) {
        title(canvas, nodeTitle(kFold), "Fold", colour::growl);
        constexpr float kExtent = 1.08f;  // headroom so the stroke is not clipped at ±1
        const Box box = nodeVisual(kFold, kVisualHeight);
        hairline(canvas, { box.x, box.bottom() }, { box.right(), box.y }, colour::track);
        const float drive = std::clamp(get(ParamId::drive) + kLaneDepth * visuals.laneValue[2], 0.0f, 1.0f);
        const auto type = static_cast<FoldType>(static_cast<int>(get(ParamId::foldType) + 0.5f));
        curve(canvas, foldTransfer(type, drive, get(ParamId::asym), kExtent), box, colour::growl, -kExtent, kExtent);
    }

    // The sum that leaves the plugin: the same phase-locked output trace the scope shows.
    void drawOut(visage::Canvas& canvas, const Visuals& visuals) {
        title(canvas, nodeTitle(kOut), "Out", colour::text);
        const Box box = nodeVisual(kOut, kOut.w - 2.0f * kInset);
        hairline(canvas, { box.x, box.midY() }, { box.right(), box.midY() }, colour::track);
        drawCurve(canvas, context_, visuals.scopeOut, px(box.x), px(box.y), px(box.w), px(box.h),
                  colour::withAlpha(colour::text, 0.8f), -1.2f, 1.2f);
    }

    void drawMidSide(visage::Canvas& canvas) {
        title(canvas, nodeTitle(kMidSide), "Mid/side", colour::text);
        constexpr float kLow = 30.0f, kHigh = 3000.0f;
        const Box box = nodeVisual(kMidSide, kMidSide.w - 2.0f * kInset);
        const float split = std::log(get(ParamId::xover) / kLow) / std::log(kHigh / kLow);
        const float sideGain = std::pow(10.0f, get(ParamId::sideGain) / 20.0f);
        const float spread = 0.8f * std::clamp(get(ParamId::width) * sideGain * 0.5f, 0.0f, 1.0f);

        hairline(canvas, { box.x + box.w * split, box.y }, { box.x + box.w * split, box.bottom() }, colour::track);
        Curve<kShapePoints> upper {}, lower {};
        for (std::size_t i = 0; i < upper.size(); ++i) {
            const float position = static_cast<float>(i) / static_cast<float>(upper.size() - 1);
            const float apart = spread * smoothstep((position - split) / 0.3f);
            upper[i] = apart;
            lower[i] = -apart;
        }
        curve(canvas, lower, box, colour::text, -1.0f, 1.0f);
        curve(canvas, upper, box, colour::text, -1.0f, 1.0f);
    }

    std::vector<std::pair<visage::Frame*, Box>> placed_;
    LaneMarker* morphMarker_ = nullptr;
    LaneMarker* driveMarker_ = nullptr;
    Watched shown_;
    float flow_ = 0.0f;
    Clock::time_point lastTick_;
};

}

std::unique_ptr<Panel> makeGraphPanel(Context& context) { return std::make_unique<GraphPanel>(context); }

}
