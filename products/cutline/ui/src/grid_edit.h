// SPDX-License-Identifier: AGPL-3.0-or-later
// The grid's logic without a window: the resolved view it draws, and the edits a click or drag makes.
// The view comes from resolveBar with the same density maps the engine plays from.
#pragma once

#include "cl/groove_state.h"
#include "cl/hits.h"
#include "cl/resolve.h"

#include <array>
#include <cstdint>
#include <string>

namespace cl::ui::grid {

enum class CellLook : std::uint8_t {
    empty,   // nothing written here
    outline, // written but silent: Density, mute, or another lane's solo
    core,
    groove,
    detail,
    fill, // sounding from the fill
};

struct Cell {
    CellLook look = CellLook::empty;
    Layer layer = Layer::groove; // the height an outline is drawn at
    std::uint8_t velocity = 0;   // the velocity that will be emitted; 0 for empty and outline
    std::uint8_t ratchet = 1;
    bool flam = false;
};

Cell cellOf(const ResolvedStep& step) noexcept;

// The same step as the fill editor shows it: lit only if it comes from the fill. A main-pattern step
// (one that shows through on a lane that draws nothing in the fill) is an outline, so a click on it
// is clearly adding a fill step, not editing a lit one.
Cell fillCellOf(const ResolvedStep& step) noexcept;

// Bar height as a fraction of the row: Core 1.0, Groove 0.72, Detail 0.44.
float heightFraction(Layer layer) noexcept;

struct View {
    int bars = 1;
    std::array<ResolvedBar, kMaxPatternBars> bar{};
};

// Every bar of `scene`, resolved for drawing. `liveFillBar` is the bar in the pattern whose fill is live, or -1.
View resolveView(const GrooveState& groove, int scene, float density, float dynamics, int liveFillBar) noexcept;

// What the fill editor shows: the pattern's last bar with its fill switched on.
ResolvedBar resolveFillView(const GrooveState& groove, int scene, float density, float dynamics) noexcept;

// Where the step cells are, in panel pixels: `steps` columns of `stepW` from `gridX`, eight rows of `rowH` from
// `gridY`.
struct Geometry {
    float gridX = 0.0f;
    float gridY = 0.0f;
    float stepW = 1.0f;
    float rowH = 1.0f;
    int steps = 16;
    int rows = kDrumLaneCount;
};

// The lane (row) and step under a point. False outside the cells.
bool cellAt(const Geometry& geometry, float x, float y, int& lane, int& step) noexcept;

// The step a cell edits: a main-pattern step, or a fill step when `fillView` is set.
// Null outside the pattern, and null before the fill window in the fill view.
// In the pattern view the column maps through the lane's loop: every repeat of a step on a looped lane
// edits the one stored step it repeats.
Step* stepAt(Pattern& pattern, bool fillView, int lane, int step) noexcept;
const Step* stepAt(const Pattern& pattern, bool fillView, int lane, int step) noexcept;

// The layer brush: a new step takes the brush's layer and its velocity, Core 112, Groove 96, Detail 64.
// A new fill step has no layer and takes velocity 96.
int brushVelocity(Layer layer) noexcept;
inline constexpr int kFillStepVelocity = 96;

// Each edit returns true when it changed something.
bool toggleStep(Pattern& pattern, bool fillView, int lane, int step,
                Layer brush = Layer::groove) noexcept; // off -> the brush; on -> off
bool paintStep(Pattern& pattern, bool fillView, int lane, int step, Layer brush) noexcept; // empty steps only
bool eraseStep(Pattern& pattern, bool fillView, int lane, int step) noexcept;              // lit steps only
bool setStepVelocity(Step& step, int velocity) noexcept;            // lit steps only, clamped to 1..127
bool cycleRatchet(Step& step) noexcept;                             // lit steps only: 1, 2, 3, 4, 1
bool setStepLayer(Step& step, Layer layer) noexcept;                // lit steps only
bool setStepNudge(Step& step, int nudge) noexcept;                  // lit steps only, clamped to -24..+24
bool toggleFlam(Step& step) noexcept;                               // lit steps only
bool setFillBeats(Pattern& pattern, int beats) noexcept;            // 1, 2 or 4
bool setLaneLength(Pattern& pattern, int lane, int steps) noexcept; // 1..pattern steps; the full length means follow

// Option-drag: one 1/96-beat nudge unit per `pxPerUnit` of horizontal travel since the press, clamped to -24..+24.
inline constexpr float kNudgeDragPx = 3.0f;
int nudgeFromDrag(int startNudge, float dx, float pxPerUnit) noexcept;

// The lane menu's pocket choices (a lane's offsetMs), and how a non-zero pocket reads in the sidebar: "+5ms".
inline constexpr std::array<int, 5> kPocketChoicesMs{-10, -5, 0, 5, 10};
bool setLanePocket(Pattern& pattern, int lane, int ms) noexcept;
std::string pocketLabel(int ms); // empty for 0

// Which way a press went first: the first axis whose travel passes the threshold.
enum class Axis : std::uint8_t { none, horizontal, vertical };

// Was a press a click or a drag? A drag is a press that ever moved past the threshold, even if it came back.
// On a lit step the first axis decides the drag: horizontal erases, vertical sets velocity.
class DragGesture {
  public:
    void begin(float x, float y) noexcept {
        startX_ = x;
        startY_ = y;
        axis_ = Axis::none;
    }
    // Returns moved(), after taking the point into account.
    bool move(float x, float y, float threshold) noexcept {
        if (axis_ != Axis::none)
            return true;
        const float dx = x > startX_ ? x - startX_ : startX_ - x;
        const float dy = y > startY_ ? y - startY_ : startY_ - y;
        if (dx > threshold && dx >= dy)
            axis_ = Axis::horizontal;
        else if (dy > threshold)
            axis_ = Axis::vertical;
        return axis_ != Axis::none;
    }
    bool moved() const noexcept { return axis_ != Axis::none; }
    Axis axis() const noexcept { return axis_; }
    float startX() const noexcept { return startX_; }
    float startY() const noexcept { return startY_; }

  private:
    float startX_ = 0.0f;
    float startY_ = 0.0f;
    Axis axis_ = Axis::none;
};

// Timing ticks: one per note-on a lit cell produces, where it starts, from forEachHit.
struct Tick {
    float at = 0.0f;         // 0 = the cell's left edge (its straight-grid start), 1 = its right edge; clamped
    bool grace = false;      // a flam's early hit, drawn dimmer
    std::int8_t outside = 0; // -1: the hit starts before the cell, +1: after it (drawn clamped, with an arrow)
};

struct CellTicks {
    std::array<Tick, 5> ticks{}; // up to four ratchet hits and a flam's grace hit
    int count = 0;
};

using BarTicks = std::array<std::array<CellTicks, kStepsPerBar>, kDrumLaneCount>;

// The timing forEachHit plays with, from the Swing parameter, the BPM and the Rate choice (0.5x, 1x, 2x).
HitTiming tickTiming(float swing, double bpm, int rateIdx) noexcept;

// A hit `beatsFromCellStart` after the start of its 16th-note cell (0.25 beats wide).
Tick tickAt(double beatsFromCellStart, bool grace) noexcept;

// The ticks of every cell of one resolved bar: forEachHit with the pattern's lane pockets, cell by cell.
void barTicks(const Pattern& pattern, const ResolvedBar& bar, const HitTiming& timing, BarTicks& out) noexcept;

// The ticks of one MIDI Slicer gate step, where it first plays in the phrase. The same forEachHit timing (Swing,
// ratchet, nudge) without lane pockets; a slicer flam is a chord strum, not a grace hit. The phrase starts no note
// before its first step.
CellTicks slicerTicks(const Step& step, int gateStep, const HitTiming& timing) noexcept;

// The phrase strip: the run of 8 bars (16 when Fill is 16) that holds the playing bar.
inline constexpr int kMaxPhraseBars = 16;

struct PhraseInput {
    bool playing = false;
    std::int64_t absBar = 0; // the playing bar on the pattern clock, as the engine counts it
    int patternBars = 1;     // of the scene that is playing
    int fillEvery = 0;       // bars between fills: 0 (Off), 2, 4, 8 or 16
    bool fillNow = false;
    int pendingScene = -1;     // a scene switch waiting for its boundary
    bool pendingOnBar = false; // that switch is quantized to 1 Bar
};

struct PhraseBar {
    std::int64_t absBar = 0;
    int patternBar = 0; // 0-based: drawn as 1..4
    bool fill = false;
    bool playing = false;
    int pendingScene = -1; // the scene that starts on this bar
};

struct Phrase {
    int length = 8;
    std::int64_t start = 0;
    std::array<PhraseBar, kMaxPhraseBars> bars{};
};

Phrase phraseOf(const PhraseInput& in) noexcept;

// The engine's bar count at a host position: floor(ppq * rate / 4).
std::int64_t phraseBarAt(double ppq, double rate) noexcept;

} // namespace cl::ui::grid
