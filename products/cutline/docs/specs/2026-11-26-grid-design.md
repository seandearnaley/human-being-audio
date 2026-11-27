# Cutline 3 grid: fast to draw, honest about time, variation on demand

Spec 2 of 4. Builds on spec 1 (`2026-11-26-groove-model-design.md`), which made the grid the literal truth of
what plays and ported a minimal editor. This spec makes the grid an instrument: drawing a beat is quick, the
grid shows when each hit lands, and the variation 2.0 hid inside playback comes back as an edit you can see and
undo.

Status: written 2026-11-26 while the founder was away. Every design decision below that the founder has not made
is listed under "Decided without the founder" so it can be overruled.

## Problem

After spec 1 the grid is truthful but thin:

- Every new step is Groove at velocity 96. Drawing a layered beat means a right-click menu per step.
- A drag paints steps on, never off. Clearing a run of hats is one click per hat.
- Nudge, flam and lane pocket exist in the model but are edited through menus (nudge) or not at all (pocket),
  and the grid does not show them: a nudged hit looks the same as an on-grid one, and Swing is invisible.
- 2.0's variation (Chop, ghosts, phrase mutation) is gone and nothing replaces it. A pattern changes only when
  the user redraws it.
- Nothing shows the phrase: which bar of the pattern is playing in the host's bar ruler, or when the next fill
  fires.
- Every grid edit shares one undo tag, so two different edits made within 450 ms undo together, and every drag
  tick serialises the whole bank into the state tree.

## Goal

- A beat with three layers can be drawn in seconds, without menus.
- Each lit cell shows where in time its hits land, including Swing, nudge, ratchet, flam and lane pocket, from
  the same timing function the engine plays with.
- One button makes a deterministic variation of the pattern, written into the grid.
- A phrase strip shows where the playhead is in the phrase and which bars will fill.
- One gesture is one undo step.

## Layer brush

Three chips in the grid header, left of Clone: `Core`, `Groove`, `Detail`. Exactly one is selected; the default
is Groove. The brush is UI state, not saved with the plugin state.

- A new step takes the brush's layer and the brush's velocity: Core 112, Groove 96, Detail 64. Detail is quiet
  by default because in practice it holds ghost notes and hat fills.
- In the fill view the chips are hidden: fill steps have no layer. New fill steps take velocity 96.
- The step menu's `Layer:` items stay.

## Paint and erase

The press decides the mode for the whole drag:

- Press on an empty cell: the cell lights (brush layer and velocity). Dragging across the same lane lights each
  empty cell the pointer passes; lit cells are left alone.
- Press on a lit cell and move horizontally first (more than 4 px horizontally before 4 px vertically): erase.
  The pressed cell and every lit cell the pointer passes on that lane go off.
- Press on a lit cell and move vertically first: velocity, as today.
- Press on a lit cell and release without moving: the cell goes off, as today.

Paint and erase stay on the lane where the press started. In the pattern view they map through a looped lane's
stored step, as clicks do today.

## Timing edits

- Option-drag (Alt on Windows) horizontally on a lit cell sets its nudge: one 1/96-beat unit per 3 px, clamped to
  -24..+24. Option-click without dragging toggles the step's flam. A press on an empty cell with Option held does
  nothing.
- The step menu's Nudge Early / Nudge Late items stay (they step by 6).
- The lane menu gains `Pocket:` items: Early 10 ms, Early 5 ms, On Grid, Late 5 ms, Late 10 ms (the lane's
  `offsetMs`: -10, -5, 0, +5, +10). The current value is ticked. A lane whose pocket is not 0 shows it in the
  sidebar after the loop length, as `+5ms` or `-10ms`.

## Timing ticks

Every lit cell (Core, Groove, Detail or fill) draws one thin vertical tick for each note-on it produces, at the
time the note starts, measured from the cell's own grid start. A step's first hit with no Swing, nudge, pocket
or flam sits on the cell's left edge; a ratchet ×3 shows three ticks; a flam shows its grace tick before the
main tick in a dimmer colour.

- The ticks come from `forEachHit` (spec 1, Timing) called with the bar the grid already resolved, the current
  Swing, the plugin's BPM and the lane pockets. The grid does not reimplement timing.
- A tick that falls before the cell's left edge or after its right edge is drawn clamped to that edge with a
  small arrow mark, so a hit pushed out of its cell is still visible as early or late.
- Outlines (written but silent steps) draw no ticks, because they produce no note-ons.
- In the MIDI Slicer gate strip the same rule applies to slicer steps.

The ticks replace the `x2` ratchet badge.

## Vary

A `Vary` button in the grid header, right of Clear, in the pattern view of drum mode. It replaces the edited
scene's pattern with a variation of it, as one undo step. Pressing it again varies the result, so repeated
presses walk further from the original. Vary is hidden in the fill view and in MIDI Slicer mode.

`cl::vary` lives in `core/include/cl/vary.h` and `core/src/vary.cpp`: a pure function of the pattern, no
allocation, no state, no `<random>`. Its choices come from a 64-bit hash of the pattern's content, so the same
pattern always gives the same variation and a test can pin the output.

```cpp
// Returns false (and leaves `out` equal to `in`) when the pattern offers nothing to vary.
bool vary(const Pattern& in, Pattern& out) noexcept;
```

Rules:

1. Core steps, lane settings (length, note, pocket, mute, solo) and the fill are never changed.
2. The target bar is the pattern's last bar when it has two or more bars (the answer to the first bar), and its
   only bar otherwise. Only steps in the target bar change. On a looped lane the target is the stored step, so a
   looped lane can change in every repeat.
3. Vary makes three moves, chosen in turn by the hash from the moves that have a candidate:
   - Shift: one Groove or Detail hit moves one step left or right to an empty step of the same lane, inside the
     target bar. All its fields move with it.
   - Add: one Detail step at velocity 64 lights on an empty odd step of the ghost, closed hat or perc A lane,
     on a lane that already has at least one step in the pattern.
   - Remove: one Groove or Detail hit goes off.
   - Ornament: a lit Groove or Detail closed-hat step gets ratchet 2, or a lit Groove or Detail snare or ghost
     step toggles its flam.
4. After the moves the pattern must still pass the spec 1 lint budgets for the target bar (Density 50%: at most 24
   note-ons and at most 3 non-Core snare, ghost or clap note-ons). Density 50% sounds exactly the Core and Groove
   steps, so `vary` counts those itself (a ratchet counts its hits, a flam counts two) without calling
   `resolveBar`, which lives in the engine. A move that would break a budget is skipped and the next move is
   tried.
5. The result is sanitised.

The hash is FNV-1a over the pattern's bytes in a fixed field order (not over the struct's memory, which has
padding). Move `i` draws from `hash + i` through SplitMix64.

## Phrase strip

A strip of bar cells between the grid header and the ruler, in drum mode.

- It shows 8 bars, or 16 when Fill is 16. The phrase is the run of host bars that contains the playing bar:
  `phraseStart = floor(absBar / length) * length`. When the transport is stopped it shows bars 1 to 8 (or 16).
- Each cell shows the pattern bar that will play there (`absBar mod pattern.bars`, as 1 to 4).
- A bar where the fill is scheduled (`fillScheduled`, the engine's own function) is drawn in the fill colour.
  While Fill Now is on, every bar from the playing one on is drawn in the fill colour.
- The playing bar is lit, with a progress bar along its bottom edge for the position in the bar.
- A pending scene switch quantized to 1 Bar marks the next bar with the pending scene's letter.
- It is display only: clicks do nothing.

## One gesture, one undo step

- `ui::Model` gains `beginGrooveEdit()` and `endGrooveEdit()`. The grid calls them on mouse-down and mouse-up
  around a press that may edit. Inside a gesture, the first `setGrooveBank` pushes one undo snapshot; later calls
  in the same gesture only publish the bank to the engine. `endGrooveEdit` writes the bank into the state tree
  once.
- A `setGrooveBank` outside a gesture (menu choices, Vary, pills) pushes its own undo snapshot and writes the
  state tree at once.
- The 450 ms coalescing tag for grid edits is removed. Other parameters keep their coalescing.
- The engine still receives every change during a drag, so a drag is heard as it happens.

## Footer

The footer hint becomes: `Drag: paint · Drag lit step: erase or velocity · ⌥ drag: nudge · ⌥ click: flam ·
Shift-click: ratchet`. In the fill view the same line without the layer part; in MIDI Slicer mode the same line
without pocket.

## Testing

Window-free logic goes in `ui/src/grid_edit.*` (brush, paint and erase decisions, nudge from drag distance) and
`core/src/vary.cpp`, with Catch2 tests in `tests/ui/GridEditTests.cpp` and a new `tests/core/VaryTests.cpp`:

- Brush: a new step takes the brush's layer and velocity; the fill view ignores the layer.
- Gesture choice: the first axis past 4 px decides erase versus velocity; a release without movement turns the
  step off.
- Erase drag turns off only lit steps on the pressed lane and never lights one.
- Nudge drag: 3 px per unit, clamped; Option-click toggles flam only on a lit step.
- Ticks: for a step with Swing, nudge, ratchet 3, flam and a pocket, the tick positions equal the times
  `forEachHit` reports for that step, relative to the cell start; a hit outside the cell is flagged early or late.
- Vary: deterministic (same input, same output); Core, lane settings and fill unchanged; only the target bar
  changes; at most 3 stored steps differ; the lint budgets hold for every factory pattern after 1, 2 and 10
  presses; returns false on an empty pattern.
- Phrase strip: phrase start, pattern bar labels, fill bars for every Fill choice, Fill Now marking, at 0.5x, 1x
  and 2x rate.
- Undo: a drag across 8 cells is one undo step; two clicks 100 ms apart are two undo steps; the state tree is
  written once per gesture (a counter on the fake model or a plugin test).

Snapshots: `cl-ui-snapshot` renders a pattern with nudges, flams, ratchets, a pocket and Swing 60% so the ticks
can be checked by eye, and the phrase strip with Fill 4 and a pending scene.

## Decided without the founder

- Brush velocities 112 / 96 / 64.
- Option-drag for nudge and Option-click for flam.
- Pocket choices limited to ±5 and ±10 ms.
- Vary makes three moves, only in the last bar, and never touches Core or fills.
- Phrase strip length 8 bars (16 when Fill is 16), display only.
- Ticks replace the ratchet badge.

## Out of scope

- Pattern browser and fill library (spec 3).
- PLAY surface (spec 4).
- Keyboard shortcuts, multi-select, copy and paste of steps.
- A Vary amount control or Vary for fills.
