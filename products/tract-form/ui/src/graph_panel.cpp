// The signal graph: Master, the two Reese voices with their feedback loop, Tract,
// Fold, Sculpt, Mid/side and Out, joined by wires, with the sub path underneath.
#include "panels.h"

#include "graph_curves.h"
#include "movement_edit.h"
#include "tf/feel.h"
#include "tf/lanes.h"
#include "tf/vowels.h"

#include <hba/core/note_name.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace tf::ui {

namespace {

using Point = WirePoint;
using Wire = SignalWire;

// A rectangle in design pixels, relative to the panel.
struct Box {
    float x, y, w, h;
    constexpr float right() const { return x + w; }
    constexpr float bottom() const { return y + h; }
    constexpr float midX() const { return x + w * 0.5f; }
    constexpr float midY() const { return y + h * 0.5f; }
};

// Seven columns, equally spaced; every node shares the same interior grid.
constexpr float kInset = 10.0f;         // node edge to content
constexpr float kTitleHeight = 16.0f;   // title band, from the top inset
constexpr float kVisualTop = 36.0f;     // from the node top
constexpr float kVisualHeight = 62.0f;
constexpr float kRowsTop = 110.0f;      // four rows end on the bottom inset
constexpr float kCompactVisualTop = 30.0f;
constexpr float kCompactVisualHeight = 56.0f;
constexpr float kCompactRowsTop = 96.0f;
constexpr float kCompactRowHeight = 20.0f;
constexpr float kReeseRowsTop = 30.0f;  // the Reese cards' 2x2 grid
constexpr float kColumnGap = 20.0f;     // between the two columns of that grid
constexpr float kSparkWidth = 90.0f;

constexpr float kPanelWidth = 1256.0f;  // the editor gives the graph its full content width
constexpr float kNodeTop = metric::pad;
constexpr float kNodeHeight = 208.0f;
constexpr float kGap = 22.0f;           // between columns
constexpr float kReeseWidth = 234.0f;

constexpr Box nextColumn(const Box& left, float width) { return { left.right() + kGap, kNodeTop, width, kNodeHeight }; }

constexpr Box kMaster { metric::pad, kNodeTop, 140.0f, kNodeHeight };
constexpr Box kReeseA { kMaster.right() + kGap, kNodeTop, kReeseWidth, 84.0f };
constexpr Box kReeseB { kReeseA.x, 104.0f, kReeseWidth, 84.0f };
constexpr Box kFeedback { kReeseA.x, 196.0f, kReeseWidth, 24.0f };
constexpr Box kTract = nextColumn(kReeseA, 160.0f);
constexpr Box kFold = nextColumn(kTract, 146.0f);
constexpr Box kSculpt = nextColumn(kFold, 150.0f);
constexpr Box kMidSide = nextColumn(kSculpt, 136.0f);
constexpr Box kOut = nextColumn(kMidSide, 134.0f);
static_assert(kOut.right() == kPanelWidth - metric::pad, "the columns fill the panel");

constexpr float kSubY = 256.0f;
constexpr float kCorner = 10.0f;
// One line: title, Split, Fit, Warmth.
constexpr float kSubLockRowX = 76.0f;  // from the card's left edge
constexpr float kLowPassWidth = 76.0f;
constexpr float kFitWidth = 60.0f;
constexpr float kWarmthWidth = 96.0f;
constexpr float kSubLockGap = 20.0f; // between rows
constexpr float kSubLockWidth =
    kSubLockRowX + kLowPassWidth + kSubLockGap + kFitWidth + kSubLockGap + kWarmthWidth + kInset;
constexpr Box kSubLock { (kMaster.midX() + kOut.midX() - kSubLockWidth) * 0.5f, kSubY - 18.0f, kSubLockWidth, 36.0f };

constexpr float kChipWidth = 52.0f;
constexpr float kTimeChipWidth = 64.0f;  // "1/4 cyc"
constexpr float kChipHeight = 20.0f;
constexpr float kChipGap = 6.0f;
constexpr float kCountChipWidth = 24.0f; // Sculpt's band count, in its title band
constexpr float kModeChipWidth = 48.0f;  // Sculpt's mode (Bell/Phaser/Comb), in its title band
constexpr float kPosChipWidth = 42.0f;   // Tract Pre/Post chip
constexpr float kAuditionWidth = 34.0f;  // Master F1 audition pill
constexpr float kCountChipHeight = 18.0f;
constexpr float kFeedbackLabelWidth = 58.0f;  // "Feedback" at label size
constexpr float kLoopOut = 10.0f;             // the feedback loop's distance from the column

// Wires: alpha with the gate open and closed, and the flow of the travelling dots.
constexpr float kWireOpen = 0.85f;
constexpr float kWireClosed = 0.55f;
constexpr float kLoopAlpha = 0.6f;
constexpr float kFlowSpeed = 56.0f;    // design pixels per second: one column gap per 0.5 s
constexpr float kDotSpacing = 112.0f;  // design pixels between dots on a wire
constexpr float kDash = 4.0f;

Box nodeTitle(const Box& node) { return { node.x + kInset, node.y + kInset, node.w - 2.0f * kInset, kTitleHeight }; }

Box nodeVisual(const Box& node, float width) {
    return { node.midX() - width * 0.5f, node.y + kVisualTop, width, kVisualHeight };
}

Box compactVisual(const Box& node, float width) {
    return {node.midX() - width * 0.5f, node.y + kCompactVisualTop, width, kCompactVisualHeight};
}

Box nodeRow(const Box& node, int index) {
    return { node.x + kInset, node.y + kRowsTop + metric::row * static_cast<float>(index), node.w - 2.0f * kInset,
             metric::row };
}

Box compactRow(const Box& node, int index) {
    return {node.x + kInset, node.y + kCompactRowsTop + kCompactRowHeight * static_cast<float>(index),
            node.w - 2.0f * kInset, kCompactRowHeight};
}

Box reeseCell(const Box& card, int column, int row) {
    const float width = (card.w - 2.0f * kInset - kColumnGap) * 0.5f;
    return { card.x + kInset + static_cast<float>(column) * (width + kColumnGap),
             card.y + kReeseRowsTop + metric::row * static_cast<float>(row), width, metric::row };
}

std::string noteName(int note) { return hba::core::midiNoteName(note); }

std::string hzText(float hz) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, hz < 100.0f ? "%.2f Hz" : "%.1f Hz", static_cast<double>(hz));
    return buffer;
}

// Whether any lane that is on moves `target`.
bool aimed(const Params& params, LaneTarget target) {
    for (int lane = 0; lane < kLaneCount; ++lane)
        if (params.on(laneParam(lane, LaneField::on)) && laneTargetOf(params, lane) == target) return true;
    return false;
}

// A parameter's value after the lanes aimed at `target` have moved it, in its range.
float moved(const Params& params, const Visuals& visuals, ParamId id, LaneTarget target) {
    return clampToRange(spec(id), laneApply(target, params[id], laneSum(params, visuals.laneValue, target)));
}

float smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

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
    wires.emplace_back(link(kFold, mid, kSculpt, mid), colour::growl);
    wires.emplace_back(link(kSculpt, mid, kMidSide, mid), colour::growl);
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

// Where the movement lanes aimed at a barred value have pushed it: a violet span
// on the row's bar from the set value to the moved one, ending in a dot. Drawn
// over the row, only while a lane that is on aims at it, and transparent to the mouse.
class LaneMarker final : public visage::Frame {
public:
    static constexpr float kAbove = 6.0f;   // design pixels from the frame top to the row's bottom edge
    static constexpr float kHeight = 10.0f;

    LaneMarker(Context& context, ParamId id, LaneTarget target) : context_(context), id_(id), target_(target) {
        setIgnoresMouseEvents(true, false);
    }

    void draw(visage::Canvas& canvas) override {
        const Params params = paramsOf(context_.model);
        if (!aimed(params, target_)) return;
        const float base = toNormalised(spec(id_), params[id_]);
        const float moved = toNormalised(spec(id_), tf::ui::moved(params, context_.model.visuals(), id_, target_));
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
    LaneTarget target_;
};

// Press-and-hold F1 audition pill in the Master title band.
class AuditionPill final : public visage::Frame {
  public:
    explicit AuditionPill(Context& context) : context_(context) {}
    ~AuditionPill() override {
        if (pressed_)
            context_.model.setAudition(false);
    }

    void draw(visage::Canvas& canvas) override {
        const float r = context_.px(metric::radiusSmall);
        const unsigned bg = pressed_   ? colour::master
                            : hovered_ ? colour::withAlpha(colour::master, 0.22f)
                                       : colour::withAlpha(colour::master, 0.12f);
        canvas.setColor(bg);
        canvas.roundedRectangle(0.0f, 0.0f, width(), height(), r);
        const unsigned fg = pressed_ ? colour::window : colour::master;
        drawLabel(canvas, context_, "F1", 0.0f, 0.0f, width(), height(), fg, visage::Font::kCenter, type::small,
                  Weight::semibold);
    }

    void mouseEnter(const visage::MouseEvent&) override {
        hovered_ = true;
        redraw();
    }
    void mouseExit(const visage::MouseEvent&) override {
        hovered_ = false;
        redraw();
    }
    void mouseDown(const visage::MouseEvent&) override {
        pressed_ = true;
        context_.model.setAudition(true);
        redraw();
    }
    void mouseUp(const visage::MouseEvent&) override {
        if (!pressed_)
            return;
        pressed_ = false;
        context_.model.setAudition(false);
        redraw();
    }

  private:
    Context& context_;
    bool hovered_ = false;
    bool pressed_ = false;
};

// Transparent 2D direct-manipulation overlay placed over a node's visual box.
// Horizontal drag moves `paramX`, vertical drag moves `paramY`, Shift = fine,
// double-click resets both to their defaults.
class DragPad final : public visage::Frame {
  public:
    DragPad(Context& context, ParamId paramX, ParamId paramY, unsigned accent)
        : context_(context), paramX_(paramX), paramY_(paramY), accent_(accent) {}

    void draw(visage::Canvas& canvas) override {
        if (!hovered_ && !dragging_)
            return;
        const float stroke = std::max(1.0f, context_.px(1.0f));
        const float r = context_.px(metric::radiusSmall);
        canvas.setColor(colour::withAlpha(accent_, dragging_ ? 0.45f : 0.22f));
        canvas.roundedRectangleBorder(0.5f * stroke, 0.5f * stroke, width() - stroke, height() - stroke, r, stroke);

        const float nx = toNormalised(spec(paramX_), context_.model.get(paramX_));
        const float ny = toNormalised(spec(paramY_), context_.model.get(paramY_));
        const float dot = context_.px(dragging_ ? 6.0f : 5.0f);
        const float cx = std::clamp(nx * width(), dot, std::max(dot, width() - dot));
        const float cy = std::clamp((1.0f - ny) * height(), dot, std::max(dot, height() - dot));
        canvas.setColor(colour::withAlpha(accent_, 0.25f));
        canvas.circle(cx - dot, cy - dot, 2.0f * dot);
        canvas.setColor(accent_);
        canvas.circle(cx - 0.5f * dot, cy - 0.5f * dot, dot);
    }

    void mouseEnter(const visage::MouseEvent&) override {
        hovered_ = true;
        redraw();
    }
    void mouseExit(const visage::MouseEvent&) override {
        hovered_ = false;
        redraw();
    }
    void mouseDown(const visage::MouseEvent& e) override {
        if (e.repeatClickCount() >= 2) {
            context_.model.begin(paramX_);
            context_.model.set(paramX_, spec(paramX_).def);
            context_.model.end(paramX_);
            context_.model.begin(paramY_);
            context_.model.set(paramY_, spec(paramY_).def);
            context_.model.end(paramY_);
            redraw();
            return;
        }
        dragging_ = true;
        startX_ = e.position.x;
        startY_ = e.position.y;
        normX_ = toNormalised(spec(paramX_), context_.model.get(paramX_));
        normY_ = toNormalised(spec(paramY_), context_.model.get(paramY_));
        context_.model.begin(paramX_);
        context_.model.begin(paramY_);
        redraw();
    }
    void mouseDrag(const visage::MouseEvent& e) override {
        if (!dragging_)
            return;
        const float fine = e.isShiftDown() ? 0.25f : 1.0f;
        const float dx = (e.position.x - startX_) / std::max(width(), 1.0f) * fine;
        const float dy = -(e.position.y - startY_) / std::max(height(), 1.0f) * fine;
        startX_ = e.position.x;
        startY_ = e.position.y;
        normX_ = std::clamp(normX_ + dx, 0.0f, 1.0f);
        normY_ = std::clamp(normY_ + dy, 0.0f, 1.0f);
        context_.model.set(paramX_, fromNormalised(spec(paramX_), normX_));
        context_.model.set(paramY_, fromNormalised(spec(paramY_), normY_));
        redraw();
    }
    void mouseUp(const visage::MouseEvent&) override {
        if (!dragging_)
            return;
        dragging_ = false;
        context_.model.end(paramX_);
        context_.model.end(paramY_);
        redraw();
    }

  private:
    Context& context_;
    ParamId paramX_;
    ParamId paramY_;
    unsigned accent_;
    bool hovered_ = false;
    bool dragging_ = false;
    float startX_ = 0.0f;
    float startY_ = 0.0f;
    float normX_ = 0.0f;
    float normY_ = 0.0f;
};

class GraphPanel final : public Panel {
public:
    explicit GraphPanel(Context& context) : Panel(context) {
        // Master: F1 audition pill + 4 standard rows (Glide lives in the studio Voice footer bar)
        const Box masterTitle = nodeTitle(kMaster);
        place<AuditionPill>({masterTitle.right() - kAuditionWidth, masterTitle.midY() - 8.0f, kAuditionWidth, 16.0f});
        place<Row>(nodeRow(kMaster, 0), ParamId::oscShape, colour::master, "Shape", true);
        place<Row>(nodeRow(kMaster, 1), ParamId::punchAmt, colour::master, "Punch", true);
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

        // Tract: Pre/Post chip + 2D DragPad + 5 compact rows
        const Box tractTitle = nodeTitle(kTract);
        place<Chip>({tractTitle.right() - kPosChipWidth, tractTitle.midY() - kCountChipHeight * 0.5f, kPosChipWidth,
                     kCountChipHeight},
                    ParamId::tractPos, colour::growl);
        place<DragPad>(compactVisual(kTract, kTract.w - 2.0f * kInset), ParamId::morph, ParamId::tractShift,
                       colour::growl);
        place<Row>(compactRow(kTract, 0), ParamId::vowelA, colour::growl, "From");
        place<Row>(compactRow(kTract, 1), ParamId::vowelB, colour::growl, "To");
        auto& morphRow = place<Row>(compactRow(kTract, 2), ParamId::morph, colour::growl, "Morph", true);
        morphRow.setModProvider([&model = context_.model]() {
            const auto& v = model.visuals();
            return v.effActive ? toNormalised(spec(ParamId::morph), v.effMorph)
                               : toNormalised(spec(ParamId::morph), model.get(ParamId::morph));
        });
        auto& shiftRow = place<Row>(compactRow(kTract, 3), ParamId::tractShift, colour::growl, "Shift", true);
        shiftRow.setModProvider([&model = context_.model]() {
            const auto& v = model.visuals();
            return v.effActive ? toNormalised(spec(ParamId::tractShift), v.effShift)
                               : toNormalised(spec(ParamId::tractShift), model.get(ParamId::tractShift));
        });
        place<Row>(compactRow(kTract, 4), ParamId::tractQ, colour::growl, "Q", true);
        markers_[0] = &place<LaneMarker>(markerBox(compactRow(kTract, 2)), ParamId::morph, LaneTarget::morph);
        markers_[1] = &place<LaneMarker>(markerBox(compactRow(kTract, 3)), ParamId::tractShift, LaneTarget::shift);

        // Fold: 2D DragPad + 4 rows
        place<DragPad>(nodeVisual(kFold, kVisualHeight), ParamId::drive, ParamId::asym, colour::growl);
        place<Row>(nodeRow(kFold, 0), ParamId::foldType, colour::growl, "Type");
        auto& driveRow = place<Row>(nodeRow(kFold, 1), ParamId::drive, colour::growl, "Drive", true);
        driveRow.setModProvider([&model = context_.model]() {
            const auto& v = model.visuals();
            return v.effActive ? toNormalised(spec(ParamId::drive), v.effDrive)
                               : toNormalised(spec(ParamId::drive), model.get(ParamId::drive));
        });
        place<Row>(nodeRow(kFold, 2), ParamId::asym, colour::growl, "Asym");
        place<Row>(nodeRow(kFold, 3), ParamId::weight, colour::growl, "Weight", true);
        markers_[2] = &place<LaneMarker>(markerBox(nodeRow(kFold, 1)), ParamId::drive, LaneTarget::drive);
        markers_[3] = &place<LaneMarker>(markerBox(nodeRow(kFold, 3)), ParamId::weight, LaneTarget::weight);

        // Sculpt: Mode + Count chips in title band + 2D DragPad + 3 rows
        const Box sculptTitle = nodeTitle(kSculpt);
        const float countX = sculptTitle.right() - kCountChipWidth;
        const float modeX = countX - 4.0f - kModeChipWidth;
        place<Chip>({modeX, sculptTitle.midY() - kCountChipHeight * 0.5f, kModeChipWidth, kCountChipHeight},
                    ParamId::sculptMode, colour::growl);
        place<Chip>({countX, sculptTitle.midY() - kCountChipHeight * 0.5f, kCountChipWidth, kCountChipHeight},
                    ParamId::sculptCount, colour::growl);
        place<DragPad>(nodeVisual(kSculpt, kSculpt.w - 2.0f * kInset), ParamId::sculpt, ParamId::sculptDepth,
                       colour::growl);
        place<Row>(nodeRow(kSculpt, 0), ParamId::sculptDepth, colour::growl, "Depth", true);
        place<Row>(nodeRow(kSculpt, 1), ParamId::sculptSpread, colour::growl, "Spread");
        place<Row>(nodeRow(kSculpt, 2), ParamId::sculptRes, colour::growl, "Resonance");

        // Mid/side: 2D DragPad + 3 rows
        place<DragPad>(nodeVisual(kMidSide, kMidSide.w - 2.0f * kInset), ParamId::xover, ParamId::sideGain,
                       colour::text);
        place<Row>(nodeRow(kMidSide, 0), ParamId::midGain, colour::text, "Mid");
        place<Row>(nodeRow(kMidSide, 1), ParamId::sideGain, colour::text, "Side");
        place<Row>(nodeRow(kMidSide, 2), ParamId::xover, colour::text, "Crossover");

        // Out: 4 rows
        place<Row>(nodeRow(kOut, 0), ParamId::growlLevel, colour::growl, "Growl", true);
        place<Row>(nodeRow(kOut, 1), ParamId::subLevel, colour::master, "Sub", true);
        place<Row>(nodeRow(kOut, 2), ParamId::punchMs, colour::master, "Punch ms");
        place<Row>(nodeRow(kOut, 3), ParamId::velSens, colour::text, "Velocity", true);

        // Sub lock: Split, Fit, Warmth
        const float subRowY = kSubLock.midY() - metric::row * 0.5f;
        const float subRowX = kSubLock.x + kSubLockRowX;
        const float fitX = subRowX + kLowPassWidth + kSubLockGap;
        const float warmthX = fitX + kFitWidth + kSubLockGap;
        place<Row>({ subRowX, subRowY, kLowPassWidth, metric::row }, ParamId::subSplit, colour::master, "Split");
        place<Row>({fitX, subRowY, kFitWidth, metric::row}, ParamId::fit, colour::master, "Fit");
        place<Row>({warmthX, subRowY, kWarmthWidth, metric::row}, ParamId::subWarmth, colour::master, "Warmth", true);

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
        if (lanesMoved || changed)
            for (LaneMarker* marker : markers_) marker->redraw();
    }

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        const Visuals& visuals = context_.model.visuals();
        drawWires(canvas, visuals.gate);

        for (const Box& node : { kMaster, kReeseA, kReeseB, kTract, kFold, kSculpt, kMidSide, kOut, kSubLock })
            drawPanel(canvas, context_, px(node.x), px(node.y), px(node.w), px(node.h), colour::raised,
                      metric::radiusSmall);

        const Params params = paramsOf(context_.model);
        drawMaster(canvas, visuals);
        // Phase lanes move the two voices opposite ways.
        const float phase = laneSum(params, visuals.laneValue, LaneTarget::phase);
        drawVoice(canvas, kReeseA, "Reese A", ParamId::aRatio, laneApply(LaneTarget::phase, get(ParamId::aPhase), phase),
                  ParamId::aPm);
        drawVoice(canvas, kReeseB, "Reese B", ParamId::bRatio, laneApply(LaneTarget::phase, get(ParamId::bPhase), -phase),
                  ParamId::bPm);
        drawFeedbackLabel(canvas);
        drawTract(canvas, params, visuals);
        drawFold(canvas, params, visuals);
        drawSculpt(canvas, params, visuals);
        drawMidSide(canvas);
        drawOut(canvas, visuals);
        title(canvas, { kSubLock.x + kInset, kSubLock.midY() - kTitleHeight * 0.5f, kSubLockRowX, kTitleHeight },
              "Sub lock", colour::master);
    }

private:
    using Clock = std::chrono::steady_clock;

    // Everything the panel's own drawing depends on.
    struct Watched {
        std::array<float, 37> params{};
        std::array<float, kLaneCount> lanes {};
        float hz = 0.0f;
        int note = -1;
        bool gate = false;
        bool operator==(const Watched&) const = default;
    };

    // The lanes' on and target decide which values they move.
    static constexpr std::array<ParamId, 37> kWatchedParams{
        ParamId::coupling,    ParamId::width,       ParamId::aRatio,      ParamId::aPhase,       ParamId::aPm,
        ParamId::bRatio,      ParamId::bPhase,      ParamId::bPm,         ParamId::vowelA,       ParamId::vowelB,
        ParamId::morph,       ParamId::foldType,    ParamId::drive,       ParamId::asym,         ParamId::reach,
        ParamId::sculpt,      ParamId::sculptDepth, ParamId::sculptCount, ParamId::sculptSpread, ParamId::sculptRes,
        ParamId::sideGain,    ParamId::xover,       ParamId::weight,      ParamId::oscShape,     ParamId::subWarmth,
        ParamId::tractShift,  ParamId::tractQ,      ParamId::tractPos,    ParamId::sculptMode,   ParamId::lane1On,
        ParamId::lane1Target, ParamId::lane2On,     ParamId::lane2Target, ParamId::lane3On,      ParamId::lane3Target,
        ParamId::lane4On,     ParamId::lane4Target,
    };

    Watched watch() {
        Watched w;
        static_assert(std::tuple_size_v<decltype(w.params)> == kWatchedParams.size());
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

    template <std::size_t N>
    void curve(visage::Canvas& canvas, const GraphCurve<N>& values, const Box& box, unsigned hue, float low, float high,
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
        const Box vis = nodeVisual(kMaster, kMaster.w - 2.0f * kInset);
        const Box waveBox{vis.x, vis.y, vis.w, vis.h - 14.0f};
        curve(canvas, subPeriod(get(ParamId::subWarmth)), waveBox, colour::master, -1.2f, 1.2f, 0.12f);

        const bool sounding = visuals.note >= 0;
        const float infoY = vis.bottom() - 13.0f;
        drawLabel(canvas, context_, sounding ? noteName(visuals.note) : "–", px(vis.x), px(infoY), px(vis.w), px(13.0f),
                  colour::textDim, visage::Font::kLeft, type::small, Weight::medium);
        if (sounding)
            drawLabel(canvas, context_, hzText(visuals.hz), px(vis.x), px(infoY), px(vis.w), px(13.0f), colour::text,
                      visage::Font::kRight, type::small, Weight::regular);
    }

    void drawVoice(visage::Canvas& canvas, const Box& card, const char* name, ParamId ratio, float phaseDegrees,
                   ParamId pm) {
        const Box band = nodeTitle(card);
        title(canvas, band, name, colour::growl);
        const auto values = voicePeriod(get(ratio), phaseDegrees, feel::pmIndex(get(pm), get(ParamId::coupling)),
                                        get(ParamId::oscShape));
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

    void drawTract(visage::Canvas& canvas, const Params& params, const Visuals& visuals) {
        title(canvas, nodeTitle(kTract), "Tract", colour::growl);
        const float morph = moved(params, visuals, ParamId::morph, LaneTarget::morph);
        const float shift = moved(params, visuals, ParamId::tractShift, LaneTarget::shift);
        const auto values =
            formantResponse(static_cast<int>(get(ParamId::vowelA) + 0.5f),
                            static_cast<int>(get(ParamId::vowelB) + 0.5f), morph, shift, get(ParamId::tractQ));
        curve(canvas, values, compactVisual(kTract, kTract.w - 2.0f * kInset), colour::growl, 0.0f, 1.08f, 0.12f);
    }

    void drawFold(visage::Canvas& canvas, const Params& params, const Visuals& visuals) {
        title(canvas, nodeTitle(kFold), "Fold", colour::growl);
        constexpr float kExtent = 1.08f;  // headroom so the stroke is not clipped at ±1
        const Box box = nodeVisual(kFold, kVisualHeight);
        hairline(canvas, { box.x, box.bottom() }, { box.right(), box.y }, colour::track);
        const float drive = moved(params, visuals, ParamId::drive, LaneTarget::drive);
        const auto type = static_cast<FoldType>(static_cast<int>(get(ParamId::foldType) + 0.5f));
        curve(canvas, foldTransfer(type, drive, get(ParamId::asym), kExtent), box, colour::growl, -kExtent, kExtent);
    }

    // The bank's response around the 0 dB line, with Reach marked where it cuts
    // off. Lanes aimed at Sculpt move the bank; one aimed at Reach adds a violet
    // hairline where they have pushed it.
    void drawSculpt(visage::Canvas& canvas, const Params& params, const Visuals& visuals) {
        title(canvas, nodeTitle(kSculpt), "Sculpt", colour::growl);
        const Box box = nodeVisual(kSculpt, kSculpt.w - 2.0f * kInset);
        // Above the highest harmonic shown, Reach has no line.
        const auto reachLine = [&](float reach, unsigned hue) {
            const float position = harmonicPosition(reach);
            if (position > 1.0f) return;
            const float x = box.x + box.w * position;
            hairline(canvas, { x, box.y }, { x, box.bottom() }, hue);
        };
        reachLine(get(ParamId::reach), colour::track);
        hairline(canvas, { box.x, box.midY() }, { box.right(), box.midY() }, colour::track);
        if (aimed(params, LaneTarget::reach))
            reachLine(moved(params, visuals, ParamId::reach, LaneTarget::reach), colour::motion);
        const auto sMode = static_cast<SculptMode>(std::clamp(static_cast<int>(get(ParamId::sculptMode) + 0.5f), 0, 2));
        const auto values =
            sculptResponse(moved(params, visuals, ParamId::sculpt, LaneTarget::sculpt),
                           static_cast<int>(get(ParamId::sculptCount) + 0.5f) + 1, get(ParamId::sculptSpread),
                           get(ParamId::sculptDepth), get(ParamId::sculptRes), sMode);
        curve(canvas, values, box, colour::growl, -kSculptRangeDb, kSculptRangeDb, 0.12f);
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
        GraphCurve<kShapePoints> upper{}, lower{};
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
    std::array<LaneMarker*, 4> markers_{}; // morph, shift, drive, weight
    Watched shown_;
    float flow_ = 0.0f;
    Clock::time_point lastTick_;
};

}

std::unique_ptr<Panel> makeGraphPanel(Context& context) { return std::make_unique<GraphPanel>(context); }

}
