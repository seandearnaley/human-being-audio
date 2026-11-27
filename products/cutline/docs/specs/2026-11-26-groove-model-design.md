# Cutline 3.0 groove model: the grid is the truth

Status: design, approved in conversation on 2026-11-26. Not yet implemented.
Spec 1 of 4. The grid, pattern-library and PLAY-surface specs follow and build on this one.

Implementation is in two plans. Plan 1 (`docs/plans/2026-11-26-groove-model-1-foundation.md`) adds the
model, `resolveBar`, timing, JSON and the seed patterns beside the 2.0 code. Plan 2 cuts the engine,
plugin and UI over and deletes the 2.0 generator.

## Problem

Cutline 2.0 plays a pattern that the user never sees. A style's bitmasks pass through a
per-cell random threshold, a random slice remap (Chop), random ratchets, automatic flams,
a hidden hat choke and an end-of-phrase fill before notes are emitted. The grid and ribbon
draw the first stage of that pipeline for two bars while the phrase runs for four.

Measured on MIDI exports of the 27 drum presets at 172 BPM:

- Fills, Chop and Ratchet are never off in any preset (minimums 26%, 8%, 16%).
- Median density is 26 note-ons per bar, with 5 snare, ghost or clap hits per bar off the backbeat.
- 7 of the 19 straight-time presets keep the snare on every 2 and 4 across the phrase.

## Goal

What the grid shows is exactly what plays, and what plays is calm by default.

- A pattern is literal steps. Nothing in playback is random.
- One Density control adds and removes hits in an authored, ranked order.
- Fills are drawn, off by default, and fire on a bar count the user chooses.
- One function decides which steps sound. The engine and the UI both call it.

## Decisions already made

- The grid is the literal truth of what plays.
- Cutline 2.0 sessions, presets and automation may break. This is Cutline 3.0.
- Model: layers plus fills (not a pure drum machine, not a bar chain).

## Pattern data

All types live in `core/include/cl/groove.h` and are fixed-size and allocation-free. `cl/pattern.h`
keeps the kit maps and lane names.

```cpp
inline constexpr int kDrumLaneCount = 8;
inline constexpr int kStepsPerBar = 16;
inline constexpr int kMaxPatternBars = 4;
inline constexpr int kMaxPatternSteps = kStepsPerBar * kMaxPatternBars; // 64
inline constexpr int kMaxSlicerSteps = 32;

enum class Layer : std::uint8_t { core = 0, groove = 1, detail = 2 };

struct Step {
    std::uint8_t velocity = 0;  // 0 = off, 1..127 = on
    Layer layer = Layer::groove;
    std::uint8_t ratchet = 1;   // 1..4 evenly spaced hits inside the step
    std::int8_t nudge = 0;      // -24..+24, units of 1/96 beat
    bool flam = false;          // one grace hit 20 ms early
};

struct DrumLane {
    std::array<Step, kMaxPatternSteps> steps{};
    std::uint8_t length = 0;    // 0 = follow the pattern; 1..64 = loop this many steps
    std::uint8_t midiNote = 0;  // 0 = kit map note; 1..127 = override
    std::int8_t offsetMs = 0;   // -20..+20, whole-lane pocket
    bool muted = false;
    bool solo = false;
};

struct Fill {
    std::array<std::array<Step, kStepsPerBar>, kDrumLaneCount> steps{};
    std::uint8_t beats = 2;     // 1, 2 or 4: how much of the bar's tail it replaces
};

struct Pattern {
    std::uint8_t bars = 1;      // 1, 2 or 4
    std::array<DrumLane, kDrumLaneCount> lanes{};
    Fill fill{};
};

struct GrooveBank {
    std::array<Pattern, kSceneCount> scenes{};            // scenes A-D
    std::uint8_t slicerLength = 16;                       // 1..32
    std::array<Step, kMaxSlicerSteps> slicerSteps{};      // literal gate steps
};
```

Rules:

- Scenes A-D are four independent patterns. There are no built-in scene biases.
- A lane with `length` in 1..(pattern steps - 1) loops its first `length` steps and restarts
  at the start of the pattern. Any other value follows the pattern.
- `Layer` is ignored on fill steps and slicer steps: if they are on, they sound.
- A newly constructed bank holds the default beat (below) in scene A and empty patterns in B-D.
  A scene the user clears is empty and stays empty.

Default beat, 1 bar, every hit Core, velocity 96:

```
kick   x.......x.......
snare  ....x.......x...
hat    x.x.x.x.x.x.x.x.
```

## Resolve: the single source of truth

`engine/include/cl/resolve.h`, implemented in `engine/src/resolve.cpp`. Pure, no state,
no allocation, `CL_NONBLOCKING`.

```cpp
struct ResolvedStep {
    std::uint8_t velocity = 0;  // 0 = silent; otherwise the exact velocity emitted
    std::uint8_t ratchet = 1;
    std::int8_t nudge = 0;
    bool flam = false;
    bool written = false;       // a step exists here (lit, or silent through Density / mute)
    bool fromFill = false;
    Layer layer = Layer::groove;
};

struct ResolvedBar {
    std::array<std::array<ResolvedStep, kStepsPerBar>, kDrumLaneCount> lanes{};
};

struct ResolveInput {
    float density = 0.5f;       // 0..1
    float dynamics = 0.5f;      // 0..1
    bool fillActive = false;    // this bar's fill window is live
};

// The Density value at or above which each stored step sounds. Rebuilt when the pattern changes.
struct DensityMap {
    std::array<std::array<float, kMaxPatternSteps>, kDrumLaneCount> onAt{};
};

void buildDensityMap(const Pattern& pattern, DensityMap& out) noexcept CL_NONBLOCKING;
void resolveBar(const Pattern& pattern, const DensityMap& map, int barInPattern, const ResolveInput& in,
                ResolvedBar& out) noexcept CL_NONBLOCKING;
// Convenience for callers that do not cache the map (UI, tests, export).
void resolveBar(const Pattern& pattern, int barInPattern, const ResolveInput& in,
                ResolvedBar& out) noexcept CL_NONBLOCKING;
```

The engine caches one `DensityMap` per scene so that ranking, the only costly part, runs when a
pattern is edited and not on every audio block.

The engine emits drum notes only from a `ResolvedBar`. The grid and ribbon draw only from a
`ResolvedBar` they compute themselves by calling the same function with the pattern bank,
the Density and Dynamics parameter values, and the fill flag from telemetry.

### Density

For the main pattern, per scene:

1. Core steps always sound.
2. Collect every on-step in the Groove layer across all lanes and bars, and sort it by rank
   (below). With `G` steps, the step at sorted index `i` sounds when
   `density >= 0.5 * (i + 1) / G`.
3. Do the same for Detail. With `D` steps, index `i` sounds when
   `density >= 0.5 + 0.5 * (i + 1) / D`.

So 0% is Core only, 50% is Core plus all of Groove, 100% is everything.
Raising Density never removes a hit; lowering it never adds one.

Rank sort key, ascending, first difference wins:

1. Metric weight of the step within its bar: step 0 is 0; step 8 is 1; steps 4 and 12 are 2;
   other even steps are 3; odd steps are 4.
2. Lane priority: kick, snare, closed hat, open hat, clap, ghost, perc A, perc B
   (lane indices 0, 1, 4, 5, 3, 2, 6, 7).
3. Bar index.
4. Step index.

A lane that loops a shorter `length` contributes only its first `length` steps, ranked by
their stored step index.

This ordering is the one musical rule in the engine. It lives in a single comparator so it
can be tuned by ear without touching anything else.

A step that is on but not sounding (Density, lane mute, or another lane's solo) resolves
with `written = true` and `velocity = 0`.

### Dynamics

Applied inside `resolveBar`, so the displayed velocity is the emitted velocity:

```
v' = clamp(round(64 + (v - 64) * (0.5 + dynamics)), 1, 127)
```

50% is identity, 0% halves the contrast around 64, 100% is 1.5x. There is no jitter and no
phrase swell.

### Fills

When `fillActive` is true, the fill window is the last `fill.beats * 4` steps of the bar. A lane
that has at least one step drawn inside the window takes the fill's steps there in place of
the main pattern's, silences included. A lane with nothing drawn inside the window keeps
playing the main pattern, so an empty fill changes nothing and a snare fill plays over the
hats. Outside the window the main pattern plays. Lane mute and solo still apply. Density
does not apply to fill steps.

The engine sets `fillActive` per bar:

```
absBar     = floor(ppq * rate / 4)             // rate is 0.5, 1 or 2
barInPattern = absBar mod pattern.bars
scheduled  = fillEvery != Off and (absBar + 1) mod fillEvery == 0
fillActive = scheduled or fillNow
```

At 1x rate, "every 4" fires in bars 4, 8, 12 of the host's bar ruler. The fill replaces the
tail of whichever pattern bar is playing in that host bar. `fillNow` is sampled once per
bar, at the earliest moment a hit in the fill window could start (a full early nudge, lane offset and
flam before the window), so that hits nudged early are not missed; switching it on later affects the
next bar.

4/4 is assumed, as in 2.0.

## Timing

Applied in one place, `forEachHit` in `engine/include/cl/hits.h`, which live playback, MIDI export and
the tests all use; none of it is random.

- Swing: odd 16th steps are delayed by `swing * 0.125` beats (0% straight, 67% triplet,
  100% dotted). Even steps never move. `swingBeat` is the same curve for any beat position and
  is strictly increasing.
- Ratchet: `ratchet` hits divide the interval from this step's swung start to the next
  step's swung start evenly.
- Nudge: `nudge / 96` beats added to the step start.
- Lane offset: `offsetMs` converted to beats at the current tempo.
- Flam: one extra note-on 20 ms before the step, same note, velocity reduced by 40 (minimum 1).
- Note length: as in 2.0 (open hat and perc B long, others from Gate).
- Hit times are not clamped. A hit may start before its bar (a flam, a negative nudge or a negative
  lane offset on the first step of a bar) or before beat 0 during host pre-roll. MIDI export deals with negative
  times itself.

Removed: the five pocket attractors and negative groove, per-style pocket tables, the
implicit open-hat choke, automatic flams.

## MIDI export

Drum export writes the active scene's pattern once through, bar 1 to the last bar, with the
current Density, Dynamics, Swing and Rate. The last bar includes the fill when Fill is not Off.

## Unchanged

Kit maps and per-lane note overrides, the audition synth, expression-lane mechanics, the
XY-to-CC modes, Rate, scene quantize, Undo/Redo and A/B.

## Parameters

Order is the automation contract. Bank 1 (first eight) is what Push and Live show first.

| # | Key | Name | Range | Default |
|---|-----|------|-------|---------|
| 1 | `density` | Density | 0-100% | 50% |
| 2 | `swing` | Swing | 0-100% | 0% |
| 3 | `dynamics` | Dynamics | 0-100% | 50% |
| 4 | `fillEvery` | Fill | Off, 2, 4, 8, 16 bars | Off |
| 5 | `scene` | Scene | A-D | A |
| 6 | `seqRate` | Rate | 0.5x, 1x, 2x | 1x |
| 7 | `padX` | Pad X | 0-100% | 50% |
| 8 | `padY` | Pad Y | 0-100% | 50% |

Then, in this order: `fillNow` (toggle, off), `engineMode`, `noteMap`, `slicerStyle`,
`sceneQuantize` (default 1 Bar), `padMidi` (default Off), `slicerLatch`, `strumMs`,
`gateLen`, `shiftMs`, `audition`, `auditionGain`, `motion`, `padProgram`, `padTouch`,
`padHold`, `padCcMode` (default Off), the two expression lanes (lane 1 default Off),
`ccThinMs`.

Removed parameters: `style`, `gravity`, `ghosts`, `chop`, `ratchet`, `complexity`, `fills`,
`phraseBars`, `attractor`, `fillType`, `variation`.

Three defaults change because they send unrequested MIDI in 2.0: `padMidi` (notes C3-G3
engaged pad programs), `padCcMode` (touching the pad sent CC74 and CC1) and expression
lane 1 (a continuous CC74 sine).

## Pad programs (interim)

The PLAY spec redesigns the performance surface. Until then the engine keeps four programs,
all locked to the grid, all returning to the pattern in phase on release:

| Program | X | Y |
|---------|---|---|
| Repeat | window: 1 beat, 1/2, 1/4, 1/8 beat | gate length |
| Roll | rate: 1/8, 1/16, 1/16T, 1/32 | velocity |
| Rate | 0.5x, 1x, 2x | below 30% kick and snare only; above 70% no kick |
| Drop | left half: silence; right half: same as `fillNow` | none |

Repeat loops the resolved steps inside its window. Roll plays the snare lane's note only.
Default program is Repeat. Slice, Bounce, Swing and Poly are removed: they drove parameters
that no longer exist.

Telemetry reports the engaged program and, for Repeat, the window's start step and length,
so the grid can show it.

## MIDI Slicer

Ported, not redesigned.

- The gate strip is `slicerSteps`: literal on/off, velocity, ratchet, flam, nudge.
- The slicer phrase is as long as the active scene's pattern.
- Bass / Stab takes its bass rhythm from the kick lane of the resolved bar.
- Chop and random ratchets are removed from the slicer; a step ratchets only if its
  `ratchet` says so.
- Clip Slice keeps its capture buffer; its Chop repeat and Ratchet echo are removed.

`phrase.h` / `phrase.cpp` shrink to the slicer compiler. `styles.h` / `styles.cpp` are deleted.

## File format

One JSON form for factory patterns, presets and plugin state, read and written by
`core/include/cl/groove_json.h`. Pattern files and presets
both carry `"format": 3` at the top level. A file without it, or with a lower number, is
rejected with the message `Cutline 2 preset: not supported in Cutline 3`. Plugin state that
fails to parse loads the default state.

A pattern:

```json
{
  "format": 3,
  "name": "Two-Step",
  "bars": 1,
  "lanes": {
    "kick":  { "core": "X.........x.....", "groove": "..x.............", "detail": ".......o........" },
    "snare": { "core": "....X.......X..." },
    "hat":   { "core": "x.x.x.x.x.x.x.x.", "detail": "...o.......o....", "offsetMs": -1 }
  },
  "fill": { "beats": 2, "lanes": { "snare": "........x.x.x.xX" } },
  "edits": [ { "lane": "hat", "step": 14, "ratchet": 2 } ]
}
```

- Lane keys: `kick`, `snare`, `ghost`, `clap`, `hat`, `openHat`, `percA`, `percB`.
- A step string has 16 characters per bar; bars may be separated by `|`. `X` = 118,
  `x` = 96, `o` = 56, `.` = off.
- A step set in more than one layer string is an error.
- A fill string is always 16 characters. Only the fill window is part of the fill: characters and
  edits before it are ignored on read and are not written back.
- `edits` carries what a character cannot: exact `velocity`, `ratchet`, `nudge`, `flam`.
  `"fill": true` on an edit targets the fill bar. The slicer object takes its own `edits`
  list, keyed by `step` alone.
- Optional lane fields: `length`, `midiNote`, `offsetMs`, `muted`, `solo`.
- The serialiser writes the nearest character and adds an edit only when the velocity is
  not exactly 118, 96 or 56.

A preset holds `name`, `category`, `format`, parameter values by key, expression curves and
`"bank": { "scenes": [pattern x4], "slicer": { "length": 16, "steps": "x..x..x...x...x." } }`.

## Factory content

Seed patterns come from the 2.0 style tables, which already hold three tiers per lane.

1. Before `styles.cpp` is deleted, a one-off tool writes one 2-bar pattern file per style
   into `products/cutline/patterns/`, for the 11 authored styles (Euclidean Lab is computed
   and is dropped). Mapping: `coreMask` to Core, else `grooveMask` to Groove, else
   `spiceMask` to Detail; `accentMask` to `X`, otherwise `x`; every ghost-lane hit to `o`;
   `pocketMs` rounded to `offsetMs`; a `defaultLen` under 16 becomes the lane `length`.
2. The same tool then demotes hits by rule until each file passes the lint below, and attaches
   one of four drawn fills. Snare Core keeps only hits present at the same step in both bars;
   then the lowest-ranked hit in an over-budget bar drops one layer at a time. A listening pass
   by ear follows in the library spec.
3. The factory preset set becomes one preset per seed pattern plus the five slicer presets,
   ported. The four Strike Body presets and the broader starter set arrive with the library spec.

Lint, run as a test over every file in `patterns/`:

- Anchor: the kick lane's Core has step 0 of every bar; the snare lane's Core is non-empty
  and identical in every bar.
- Core only (Density 0%): at most 16 note-ons in every bar.
- Density 50%: at most 24 note-ons in every bar, and at most 3 snare, ghost or clap
  note-ons in any bar that are not Core.
- Fill: `beats` is 1 or 2, with at least one note-on and at most 6 per beat of fill.

Ratchet hits and flams count as note-ons.

## UI: minimal port

Enough for the plugin to stay usable; the grid spec does the real design.

- The plugin opens on GRID.
- The grid shows every bar of the pattern (16, 32 or 64 columns) and draws `resolveBar`
  output: Core, Groove and Detail at three heights; `written` but silent steps as an outline;
  fill steps in a distinct colour while the fill is live.
- The playhead comes from telemetry as an absolute step in the pattern.
- Click toggles a step (new steps: Groove, velocity 96). Vertical drag sets velocity and
  shift-click cycles ratchet, as today. The step's right-click menu sets its layer.
- A Fill toggle switches the grid to the fill bar for editing.
- Macro row: Density, Swing, Dynamics, Fill.
- Removed from the grid toolbar: Mutate, Polymeter, lane Lock. Clone and Clear stay.
- PLAY shows the four interim pad programs. Its art is unchanged.
- The pulse ribbon draws from `resolveBar`.

## Telemetry

The 768-note `PhrasePlan` snapshot for drums is replaced by position state: active and
pending scene, absolute bar, bar in pattern, step in bar, whether this bar's fill is live,
pad program and engagement, Repeat window, lane flashes. The slicer keeps its plan snapshot.

## Engine structure

`engine.cpp` is 1,104 lines and mixes transport, drum playback, five pad programs, the clip
slicer and CC output. This work splits it along the seams it touches:

- `resolve.cpp`: `resolveBar` and the rank comparator. No dependencies beyond `groove.h`.
- `engine.cpp`: transport, scheduling, note-off tracking, emitting resolved bars.
- `pad_programs.cpp`: the four interim programs.

CC and expression-lane output stays where it is.

## Testing

Catch2, in `tests/core` and `tests/engine`:

1. Truth: for seeded random patterns, densities, swing values and fill settings, with pads
   disengaged and expression lanes off, over 16 bars: the multiset of (lane, bar, step)
   note-ons the engine emits equals the cells `resolveBar` reports as sounding, counting
   `ratchet + (flam ? 1 : 0)` per cell, and each note-on's velocity equals the resolved
   velocity (the flam grace hit excepted).
2. Determinism: two engines given the same inputs produce byte-identical MIDI; so does one
   engine after `reset()`.
3. Density is monotonic, and Core always sounds.
4. Fill schedule: fires only where `(absBar + 1) mod fillEvery == 0`, replaces exactly the
   last `beats * 4` steps, and `fillNow` forces it.
5. Swing leaves even steps unmoved and never reorders notes.
6. JSON round-trips a bank with every field set; a format-less preset is rejected.
7. Factory lint passes for every pattern file.
8. Pads leave no stuck notes, and a C3 note-on does not engage a pad with default parameters.
9. The existing `-Werror=function-effects` and RealtimeSanitizer gates pass.

The 2.0 tests for styles, the eight pad programs and locked-lane Mutate are deleted with the
code they cover.

## What is cut

Style as a live parameter; Ghosts, Chop, Ratchet, Complex, Fills amount, Fill type, Var,
Pocket and Bars; the auto / forced-on / forced-off cell states; lane Lock; Euclidean
overrides and the Euclidean Lab style; Mutate and Polymeter actions; built-in scene biases;
the open-hat choke; automatic flams; velocity jitter and phrase swell; the Slice, Bounce,
Swing and Poly pad programs; 8-bar phrases; all random number use in playback.

## Out of scope

- Grid interaction design, the layer brush, the phrase strip, a Vary tool (grid spec).
- The broader starter pattern set, a pattern browser, Strike Body presets (library spec).
- The PLAY surface and its visuals (PLAY spec).
- Release work: version bump, release notes, README and website copy, demos.
- Time signatures other than 4/4.
- A fill that silences lanes it does not draw (a deliberate drop). Use the Drop pad for now.
