// SPDX-License-Identifier: AGPL-3.0-or-later
// The grid's logic without a window: the resolved view it draws, and the edits a click or drag makes.
// The view comes from resolveBar with the same density maps the engine plays from.
#pragma once

#include "cl/groove_state.h"
#include "cl/resolve.h"

#include <array>
#include <cstdint>

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
};

// The lane and step under a point. False outside the cells.
bool cellAt(const Geometry& geometry, float x, float y, int& lane, int& step) noexcept;

// The step a cell edits: a main-pattern step, or a fill step when `fillView` is set.
// Null outside the pattern, and null before the fill window in the fill view.
Step* stepAt(Pattern& pattern, bool fillView, int lane, int step) noexcept;

// Each edit returns true when it changed something.
bool toggleStep(Pattern& pattern, bool fillView, int lane, int step) noexcept; // off -> Groove at 96; on -> off
bool setStepVelocity(Step& step, int velocity) noexcept;                       // lit steps only, clamped to 1..127
bool cycleRatchet(Step& step) noexcept;                                        // lit steps only: 1, 2, 3, 4, 1
bool setStepLayer(Step& step, Layer layer) noexcept;                           // lit steps only
bool setFillBeats(Pattern& pattern, int beats) noexcept;                       // 1, 2 or 4
bool setLaneLength(Pattern& pattern, int lane, int steps) noexcept; // 1..pattern steps; the full length means follow

} // namespace cl::ui::grid
