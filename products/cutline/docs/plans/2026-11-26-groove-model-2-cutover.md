# Cutline 3 Groove Model, Plan 2 of 2: Cutover

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut the Cutline engine, plugin and UI over to the groove model that Plan 1 added, so that the grid shows exactly what plays, and delete the Cutline 2.0 generator.

**Architecture:** The engine emits drum notes only through `resolveBar` and `forEachHit`; the grid and ribbon draw the same `resolveBar` output from the same pattern bank and density maps. The parameter contract, presets and plugin state move to the new model with `"format": 3`, and Cutline 2 data is rejected or replaced by the default state. The four interim pad programs move to their own file, and the MIDI Slicer is ported onto literal gate steps.

**Tech Stack:** C++20, CMake presets (`engine`, `dev`), Catch2, JUCE 9, Visage 1.0, nlohmann/json through `hba::core::Json`, Python 3 for the factory preset builder.

**Spec:** [`docs/specs/2026-11-26-groove-model-design.md`](../specs/2026-11-26-groove-model-design.md). It is the authority. Where this plan and the spec disagree, stop and report it.

**Plan 1:** [`2026-11-26-groove-model-1-foundation.md`](2026-11-26-groove-model-1-foundation.md). Its last section, "Carried into Plan 2 from the reviews", is answered item by item in the table below.

## Global Constraints

- Work on branch `cutline-3-cutover`, which starts at `065120d`, the tip of `cutline-3-groove-model`. Never commit to, move, rebase, reset or delete `cutline-3-groove-model`: it holds only Plan 1's additive work, so that its pull request stays small. Do not create another branch or a worktree. Do not push either branch and do not open a pull request.
- All commands run from `products/cutline` unless a step says otherwise.
- Configure with `-DCPM_SOURCE_CACHE="$HOME/.cache/CPM"`. Both build directories (`build/engine`, `build/dev`) already exist and are configured.
- Every new C++ file starts with `// SPDX-License-Identifier: AGPL-3.0-or-later`.
- Studio targets compile with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.
- Every function marked `CL_NONBLOCKING` must be allocation-free, lock-free and syscall-free, and must pass `-Wfunction-effects -Werror=function-effects`. `cl_engine` is built with that flag on this Mac, so a violation is a compile error here.
- No random numbers anywhere in playback: no `Rng`, no `laneHash`, no `uniform()` in `core/`, `engine/src/engine*.cpp`, `engine/src/pad_programs.cpp`, `engine/src/phrase.cpp`, `engine/src/resolve.cpp`, `engine/src/smf.cpp`. The audition synth's noise oscillator (`engine/src/audition.h`) is a sound source and stays. The expression lanes' "Steps" shape (`hba::dsp::laneStepValue`, in the shared DSP module) is a fixed function of lane and step, is unchanged by the spec, and stays; the unused `using hba::dsp::laneHash;` in `core/include/cl/lanes.h` is deleted in Task 4. Tests may use a seeded generator to build patterns.
- 4/4 only: 16 steps per bar, 4 beats per bar.
- Lane order is fixed: 0 kick, 1 snare, 2 ghost, 3 clap, 4 closed hat, 5 open hat, 6 perc A, 7 perc B.
- `clang-format` is not on `PATH`. Use `xcrun clang-format --style=file:../../.clang-format`. Format only files you created or rewrote; in files you edited in part, format only the lines you touched.
- The `rtsan` preset is Linux-only and runs in CI. Do not try to run it here. Tests that call `Engine::process` carry the `[engine]` tag, because CI runs that tag under RealtimeSanitizer.
- Commit messages have no attribution trailer lines.
- A task's implementer may fix compile errors in this plan's code, but must keep every interface in the task's **Interfaces** block and every test in the task exactly as written. If a test cannot pass as written, stop and report it instead of changing the test.

Unlike Plan 1, the code in this plan was not compiled before it was written down. The tests and the interface blocks are the contract; the implementation blocks are the intended shape.

### What builds when

The tree cannot build as a whole between Task 2 and Task 6. Each task names the targets that must build and the tests that must pass when it ends.

| After task | Must build | Must pass | Known broken until |
|---|---|---|---|
| 1 | everything in the `engine` preset | `ctest --preset engine` | none |
| 2 | `cl_core`, `cl-core-tests` | `build/engine/tests/cl-core-tests` | `cl_engine`, tools, engine tests (Task 3/4); UI and plugin (Task 5/6); `factory/showcase` (Task 7) |
| 3 | `cl_engine`, `cl-engine-tests`, `cl-core-tests` | both test binaries | `cutline-render` (Task 4); UI and plugin (Task 5/6); `factory/showcase` (Task 7) |
| 4 | everything in the `engine` preset | `ctest --preset engine` | UI and plugin (Task 5/6); `factory/showcase` (Task 7) |
| 5 | `cl_ui`, `cl-ui-tests`, `cl-ui-snapshot` in the `dev` preset | `build/dev/tests/cl-ui-tests` | plugin (Task 6); `factory/showcase` (Task 7) |
| 6 | everything in both presets | `ctest --preset engine`, `ctest --preset dev` | `factory/showcase` (Task 7) |
| 7 | `factory/showcase` | it compiles | none |
| 8 | everything | everything | none |

While a preset is partly broken, do not run `ctest --preset ...` for it: CTest reports every unbuilt test binary as a failure. Run the test binaries named in the task directly.

### Carried items from Plan 1's reviews

| Item | Where it is handled |
|---|---|
| Build each scene's `DensityMap` on the message thread and publish it with the bank | Task 3 (`GrooveState`), Task 6 (`PatternStore`) |
| `forEachHit` emits lane by lane, not in time order | Task 3: hits are filtered by block window, collected in a fixed buffer and sorted before notes are triggered |
| MIDI export must handle negative hit times | Task 4: export wraps times into the clip, which is what the looping plugin plays |
| Derive the `fillNow` lookahead from the largest early shift | Task 1 (`maxEarlyBeats`), Task 3 (fill latch). The spec's "half a beat" wording is updated in Task 3 |
| Clamp `offsetMs` to -20..+20 on the play path | Task 1 (`forEachHit` clamps, `sanitise` clamps) |
| One rule for steps hidden by a shorter `bars` or `slicerLength` | Task 1: `sanitise` clears them. Memory, files and the engine hold the same bank |
| JSON strictness | Task 1: an edit with a missing or out-of-range `step` is an error in patterns and in the slicer alike; an edit may not switch on a step that has no character |
| Guard `resolveBar` against a NaN `density` | Task 1 (Density and Dynamics both) |
| Tests to add beside the truth test | Task 1 |
| Small hygiene (`hits.h` lane names, `<cstddef>`, epsilon comment) | Task 1 |
| `hits.h` first compiled under `-Werror=function-effects` | Task 3, when `engine.cpp` includes it |
| For the library spec: lint anchors through `storedStep()`, `files == 11` as a floor, Amen and Drill by ear | Deferred to the library spec, as Plan 1 says |

### Decisions this plan makes where the spec is silent

- **Default slicer gate.** A new bank's slicer strip is `x..x..x...x...x.` (the spec's own example), not empty. Otherwise switching the default state to MIDI Slicer plays nothing.
- **Pattern length control.** The grid header gets `1 | 2 | 4` bar pills, and the fill view gets `1 | 2 | 4` beat pills. Growing a pattern copies the existing bars into the new ones, so a lane that follows the pattern sounds the same until edited. A lane with its own loop length keeps that length and loops on across the new bars instead of restarting each bar (corrected in the final fix round: a 12-step lane in a 1-bar pattern plays 0..11, 0..3; grown to 2 bars it plays 0..11, 0..11, 0..7). Shrinking drops the hidden bars.
- **Pads in MIDI Slicer mode.** Repeat loops the slicer's notes inside its window, Roll retriggers the held notes, Rate works as in drum mode, Drop's left half is silence and its right half does nothing.
- **Pattern presets carry default parameters.** The 11 pattern presets are the pattern plus defaults (Swing 0%). The five slicer presets carry their 2.0 Swing where their 2.0 pocket was MPC Swing. They do not carry their 2.0 Dynamics (0.70 to 0.76): that knob meant accent, jitter and swell amount in 2.0 and means velocity contrast around 64 in 3.0, where 50% is the identity, so they load at 50%. Per-preset Swing for the pattern presets waits for the library spec's listening pass.
- **Factory categories.** Breaks (Amen, Think, Electro), Neuro (Neuro, Halftime), Club (UKG, Footwork, Stepper, Trap, Drill, Afro), MIDI Slicer (5).
- **MIDI export length.** The clip is the pattern once through at the current Rate: `bars * 4 / rate` host beats.

### Deferred, with reasons

- **Release recall fixture** (`factory/release/fixtures/cutline.vststate` and the `cutline` entry in `parameters.json`, which names the removed `gravity` and `ratchet`). It is only read by `release.yml` when a release is cut, and a new fixture has to be captured from a built Cutline 3 VST3 with `pedalboard`. That is release work, which the spec puts out of scope. It must be regenerated before Cutline 3 is released.
- **Showcase audio.** Task 7 keeps `factory/showcase` compiling, because CI builds it. Re-rendering the website demo clips is out of scope.
- **Version bump, release notes, website copy.** Out of scope. `CMakeLists.txt` stays at `2.0.0`.
- **Strike Body presets and the wider starter set.** Library spec.

## File Structure

| File | Change | Responsibility after this plan |
|------|--------|--------------------------------|
| `core/include/cl/groove.h` | modify | Adds `sanitise`, `setPatternBars`, the default slicer gate. |
| `core/src/groove_json.cpp` | modify | Strict edits; reader output is sanitised. |
| `core/include/cl/params.h` | rewrite | The Cutline 3 parameter contract (42 parameters). |
| `core/include/cl/pattern.h`, `core/src/pattern.cpp` | rewrite | Kit maps, lane names, pad labels, `effectiveLaneMidiNote`, `midiNoteLabel`. All 2.0 pattern types are gone. |
| `core/include/cl/preset.h`, `core/src/preset.cpp` | rewrite | Presets on `GrooveBank` with `"format": 3`. |
| `engine/include/cl/hits.h` | modify | Named long-note lanes, `offsetMs` clamp, `maxEarlyBeats`, `maxLateBeats`. |
| `engine/include/cl/resolve.h`, `engine/src/resolve.cpp` | modify | NaN guards; `GrooveState` constructor. |
| `engine/include/cl/groove_state.h` | create | `GrooveState`: a sanitised bank plus one `DensityMap` per scene. |
| `engine/include/cl/held_notes.h` | create | `HeldNotes`, moved out of `phrase.h`. |
| `engine/include/cl/engine.h` | rewrite | Engine API: `setGroove`, `position()`, `slicerPlan()`. |
| `engine/src/engine_impl.h` | create | `Engine::Impl` declaration, shared by the two files below. |
| `engine/src/engine.cpp` | rewrite | Transport, scheduling, note-off tracking, emitting resolved bars, slicer and clip playback, CC output. |
| `engine/src/pad_programs.h`, `engine/src/pad_programs.cpp` | create | The four interim pad programs and their X/Y mappings. |
| `engine/include/cl/phrase.h`, `engine/src/phrase.cpp` | rewrite | The slicer compiler only. |
| `engine/include/cl/smf.h`, `engine/src/smf.cpp` | rewrite | MIDI export through `resolveBar` and `forEachHit`. |
| `engine/src/styles.cpp`, `engine/src/styles.h`, `tools/seed_patterns.py` | delete | |
| `tools/cutline_render.cpp` | modify | New engine and export API. |
| `tools/build_presets.py` | create | Builds `presets/*.json` from `patterns/*.json`. |
| `presets/*.json` | replace | 11 pattern presets and 5 slicer presets. |
| `ui/include/cl/ui/model.h`, `fake_model.h`, `ui/src/fake_model.cpp` | rewrite | Model interface on `GrooveState`; position telemetry in `Visuals`. |
| `ui/src/grid_edit.h`, `ui/src/grid_edit.cpp` | create | Grid logic without a window: resolved view, cell looks, step edits. |
| `ui/src/grid_stage.h`, `grid_stage_draw.cpp`, `grid_stage_input.cpp` | rewrite | The grid draws `resolveBar` output; layer and fill editing. |
| `ui/src/pulse_ribbon.cpp`, `setup_panel.cpp`, `play_panel.{h,cpp}`, `pad_art.{h,cpp}`, `theme.h`, `editor.cpp`, `ui/tools/snapshot.cpp` | modify | Ribbon from `resolveBar`; macro row; four pad programs; GRID as the opening view. |
| `plugin/src/PatternStore.{h,cpp}`, `Telemetry.h`, `PluginProcessor.{h,cpp}`, `PluginModel.{h,cpp}`, `Presets.cpp`, `MidiExporter.{h,cpp}`, `PluginEditor.cpp` | modify | Plugin glue on the new model; state versioning. |
| `tests/core/CoreTests.cpp`, `tests/engine/EngineTests.cpp`, `tests/plugin/PluginTests.cpp` | rewrite | |
| `tests/core/PresetTests.cpp`, `tests/core/FactoryPresetTests.cpp`, `tests/engine/PadTests.cpp`, `tests/engine/SlicerTests.cpp`, `tests/engine/SmfTests.cpp`, `tests/ui/GridEditTests.cpp` | create | |
| `factory/showcase/clips_cutline.cpp` (repository root) | modify | Compiles against the new API. |
| `README.md` | rewrite | Describes Cutline 3. |

## Before you start

- [x] **Confirm the branch and a clean product tree**

```bash
git branch --show-current && git status --short -- .
```

Expected: `cutline-3-cutover`, and no lines for files under `products/cutline` other than this plan if it is not committed yet.

- [x] **Confirm the foundation branch has not moved**

```bash
git rev-parse --short cutline-3-groove-model && git merge-base --is-ancestor cutline-3-groove-model HEAD && echo "foundation is an ancestor"
```

Expected: `065120d`, then `foundation is an ancestor`.

- [x] **Confirm the baseline is green**

```bash
cmake --build --preset engine && ctest --preset engine 2>&1 | tail -3
```

Expected: `100% tests passed, 0 tests failed out of 40`.

---

### Task 1: Harden the foundation

Everything here touches only Plan 1's files. Nothing in the 2.0 engine calls them, so the whole `engine` preset stays green.

**Files:**
- Modify: `core/include/cl/groove.h`
- Modify: `core/src/groove_json.cpp`
- Modify: `engine/include/cl/hits.h`
- Modify: `engine/src/resolve.cpp`
- Modify: `tests/core/GrooveTests.cpp`, `tests/core/GrooveJsonTests.cpp`, `tests/engine/ResolveTests.cpp`, `tests/engine/HitsTests.cpp`

**Interfaces:**
- Consumes: everything Plan 1 added.
- Produces, in `cl/groove.h`: `inline constexpr std::array<int, 5> kDefaultSlicerGate{0, 3, 6, 10, 14}`; `constexpr void sanitise(Step&) noexcept`; `constexpr void sanitise(Pattern&) noexcept`; `constexpr void sanitise(GrooveBank&) noexcept`; `constexpr void setPatternBars(Pattern&, int bars) noexcept`. `GrooveBank()` now also switches on the default slicer gate at velocity 96.
- Produces, in `cl/hits.h`: `inline constexpr int kOpenHatLane = 5`, `kPercBLane = 7`, `kMaxNudge = 24`, `kMaxOffsetMs = 20`; `constexpr bool isLongNoteLane(int lane) noexcept`; `inline double maxEarlyBeats(double beatsPerMs) noexcept`; `inline double maxLateBeats(double beatsPerMs) noexcept`.
- Behaviour changes: `resolveBar` treats a non-finite Density as 0 and a non-finite Dynamics as 0.5. `forEachHit` clamps `offsetMs` to -20..+20. `readPattern` and `readGrooveBank` return sanitised data and reject bad edits.

- [x] **Step 1: Write the failing model tests**

Append to `tests/core/GrooveTests.cpp`:

```cpp
TEST_CASE("A new bank has the default slicer gate", "[groove]") {
    const GrooveBank bank;
    for (int step = 0; step < kMaxSlicerSteps; ++step) {
        const bool expected = step == 0 || step == 3 || step == 6 || step == 10 || step == 14;
        CHECK(bank.slicerSteps[static_cast<std::size_t>(step)].on() == expected);
    }
    CHECK(bank.slicerSteps[0].velocity == 96);
}

TEST_CASE("Sanitise blanks off steps, clamps fields and clears what the lengths hide", "[groove]") {
    GrooveBank bank;
    Pattern& p = bank.scenes[1];
    p.bars = 3; // rounds down to 2
    p.lanes[0].steps[5].ratchet = 3; // off step carrying junk
    p.lanes[0].steps[6].velocity = 200;
    p.lanes[0].steps[6].ratchet = 9;
    p.lanes[0].steps[6].nudge = 90;
    p.lanes[0].steps[40].velocity = 96; // beyond 2 bars
    p.lanes[0].offsetMs = 77;
    p.lanes[0].midiNote = 200;
    p.lanes[0].length = 40; // not shorter than the pattern: follow it
    p.lanes[1].length = 7;
    p.fill.beats = 3; // rounds to 2
    p.fill.steps[1][4].velocity = 96;  // before the 2-beat window
    p.fill.steps[1][12].velocity = 96; // inside it
    p.fill.steps[1][12].layer = Layer::core;
    bank.slicerLength = 60;
    bank.slicerSteps[31].velocity = 96;
    bank.slicerSteps[31].layer = Layer::detail;

    sanitise(bank);

    CHECK(p.bars == 2);
    CHECK(p.lanes[0].steps[5] == Step{});
    CHECK(p.lanes[0].steps[6].velocity == 127);
    CHECK(p.lanes[0].steps[6].ratchet == 4);
    CHECK(p.lanes[0].steps[6].nudge == 24);
    CHECK_FALSE(p.lanes[0].steps[40].on());
    CHECK(p.lanes[0].offsetMs == 20);
    CHECK(p.lanes[0].midiNote == 127);
    CHECK(p.lanes[0].length == 0);
    CHECK(p.lanes[1].length == 7);
    CHECK(p.fill.beats == 2);
    CHECK_FALSE(p.fill.steps[1][4].on());
    CHECK(p.fill.steps[1][12].on());
    CHECK(p.fill.steps[1][12].layer == Layer::groove); // layer means nothing on a fill step
    CHECK(bank.slicerLength == 32);
    CHECK(bank.slicerSteps[31].layer == Layer::groove);

    bank.slicerLength = 8;
    sanitise(bank);
    CHECK_FALSE(bank.slicerSteps[10].on()); // the default gate's step 10 is now hidden

    const GrooveBank once = bank;
    sanitise(bank);
    CHECK(bank == once); // sanitising twice changes nothing
}

TEST_CASE("Growing a pattern repeats it and shrinking drops the hidden bars", "[groove]") {
    Pattern p = defaultBeat();
    p.lanes[6].length = 12;
    setPatternBars(p, 2);
    CHECK(patternSteps(p) == 32);
    CHECK(p.lanes[0].steps[16].on());
    CHECK(p.lanes[1].steps[20].on());
    CHECK(p.lanes[4].steps[30].on());
    CHECK(p.lanes[6].length == 12);

    p.lanes[0].steps[20].velocity = 96;
    setPatternBars(p, 1);
    CHECK(patternSteps(p) == 16);
    CHECK_FALSE(p.lanes[0].steps[16].on());
    CHECK_FALSE(p.lanes[0].steps[20].on());

    p.lanes[6].length = 16;
    setPatternBars(p, 1);
    CHECK(p.lanes[6].length == 0);
}
```

- [x] **Step 2: Run them to verify they fail**

```bash
cmake --build --preset engine --target cl-core-tests
```

Expected: the build fails with `use of undeclared identifier 'sanitise'`.

- [x] **Step 3: Add the model code**

In `core/include/cl/groove.h`, add `#include <algorithm>` and `#include <cstddef>` to the includes. Replace the `GrooveBank` struct with:

```cpp
// The default slicer gate: x..x..x...x...x.
inline constexpr std::array<int, 5> kDefaultSlicerGate{0, 3, 6, 10, 14};

struct GrooveBank {
    std::array<Pattern, kSceneCount> scenes{}; // scenes A-D
    std::uint8_t slicerLength = 16;            // 1..32
    std::array<Step, kMaxSlicerSteps> slicerSteps{};

    // A new bank holds the default beat in scene A, empty patterns in B-D and the default slicer gate.
    constexpr GrooveBank() noexcept CL_NONBLOCKING {
        scenes[0] = defaultBeat();
        for (const int step : kDefaultSlicerGate)
            slicerSteps[static_cast<std::size_t>(step)].velocity = 96;
    }

    bool operator==(const GrooveBank&) const noexcept = default;
};
```

At the end of the file, before the closing namespace brace, add:

```cpp
// Canonical form. Memory, files and the engine all hold sanitised banks, so they agree on what exists:
// an off step is blank, every field is in range, and steps hidden by the pattern length, the fill
// window or the slicer length are cleared.
constexpr void sanitise(Step& s) noexcept CL_NONBLOCKING {
    if (s.velocity == 0) {
        s = {};
        return;
    }
    s.velocity = static_cast<std::uint8_t>(std::min(static_cast<int>(s.velocity), 127));
    if (static_cast<int>(s.layer) > static_cast<int>(Layer::detail))
        s.layer = Layer::groove;
    s.ratchet = static_cast<std::uint8_t>(std::clamp(static_cast<int>(s.ratchet), 1, 4));
    s.nudge = static_cast<std::int8_t>(std::clamp(static_cast<int>(s.nudge), -24, 24));
}

constexpr void sanitise(Pattern& p) noexcept CL_NONBLOCKING {
    p.bars = static_cast<std::uint8_t>(patternBars(p));
    const int steps = patternSteps(p);
    for (auto& lane : p.lanes) {
        for (int step = 0; step < kMaxPatternSteps; ++step) {
            Step& s = lane.steps[static_cast<std::size_t>(step)];
            if (step >= steps)
                s = {};
            else
                sanitise(s);
        }
        if (static_cast<int>(lane.length) >= steps)
            lane.length = 0;
        lane.midiNote = static_cast<std::uint8_t>(std::min(static_cast<int>(lane.midiNote), 127));
        lane.offsetMs = static_cast<std::int8_t>(std::clamp(static_cast<int>(lane.offsetMs), -20, 20));
    }
    const int fillFrom = fillWindowStart(p.fill);
    p.fill.beats = static_cast<std::uint8_t>((kStepsPerBar - fillFrom) / 4);
    for (auto& row : p.fill.steps) {
        for (int step = 0; step < kStepsPerBar; ++step) {
            Step& s = row[static_cast<std::size_t>(step)];
            if (step < fillFrom) {
                s = {};
            } else {
                sanitise(s);
                if (s.on())
                    s.layer = Layer::groove;
            }
        }
    }
}

constexpr void sanitise(GrooveBank& bank) noexcept CL_NONBLOCKING {
    for (auto& pattern : bank.scenes)
        sanitise(pattern);
    bank.slicerLength = static_cast<std::uint8_t>(std::clamp(static_cast<int>(bank.slicerLength), 1, kMaxSlicerSteps));
    for (int step = 0; step < kMaxSlicerSteps; ++step) {
        Step& s = bank.slicerSteps[static_cast<std::size_t>(step)];
        if (step >= static_cast<int>(bank.slicerLength)) {
            s = {};
        } else {
            sanitise(s);
            if (s.on())
                s.layer = Layer::groove;
        }
    }
}

// Sets the pattern length to 1, 2 or 4 bars. Growing repeats the existing bars into the new ones,
// so the pattern sounds the same until it is edited. Shrinking clears the bars that are no longer played.
constexpr void setPatternBars(Pattern& p, int bars) noexcept CL_NONBLOCKING {
    const int from = patternSteps(p);
    p.bars = static_cast<std::uint8_t>(bars >= 4 ? 4 : (bars >= 2 ? 2 : 1));
    const int to = patternSteps(p);
    for (auto& lane : p.lanes) {
        for (int step = from; step < to; ++step)
            lane.steps[static_cast<std::size_t>(step)] = lane.steps[static_cast<std::size_t>(step % from)];
        for (int step = to; step < kMaxPatternSteps; ++step)
            lane.steps[static_cast<std::size_t>(step)] = {};
        if (static_cast<int>(lane.length) >= to)
            lane.length = 0;
    }
}
```

- [x] **Step 4: Run the model tests**

```bash
cmake --build --preset engine --target cl-core-tests && build/engine/tests/cl-core-tests "[groove]"
```

Expected: `All tests passed`. One existing test, "Patterns and banks round-trip with every field set", may now fail because the reader adds file steps on top of the default gate. Step 6 fixes the reader; carry on.

- [x] **Step 5: Write the failing JSON tests**

Append to `tests/core/GrooveJsonTests.cpp`:

```cpp
TEST_CASE("Edits reach main, fill and slicer steps", "[groove][json]") {
    std::string error;
    const auto p = parsePattern(R"({
        "format": 3,
        "lanes": { "hat": { "groove": "x.x............." } },
        "fill": { "beats": 1, "lanes": { "snare": "............x.x." } },
        "edits": [
            { "lane": "hat", "step": 2, "velocity": 71, "ratchet": 3, "nudge": -6, "flam": true },
            { "lane": "snare", "step": 14, "fill": true, "ratchet": 2 }
        ]
    })",
                                &error);
    INFO(error);
    REQUIRE(p.has_value());
    CHECK(p->lanes[4].steps[2].velocity == 71);
    CHECK(p->lanes[4].steps[2].ratchet == 3);
    CHECK(p->lanes[4].steps[2].nudge == -6);
    CHECK(p->lanes[4].steps[2].flam);
    CHECK(p->fill.steps[1][14].ratchet == 2);

    const auto bank = parseGrooveBank(
        R"({"scenes":[],"slicer":{"length":8,"steps":"x..x....","edits":[{"step":3,"velocity":40,"flam":true}]}})",
        &error);
    INFO(error);
    REQUIRE(bank.has_value());
    CHECK(bank->slicerLength == 8);
    CHECK(bank->slicerSteps[3].velocity == 40);
    CHECK(bank->slicerSteps[3].flam);
    CHECK_FALSE(bank->slicerSteps[6].on()); // the file's strip replaces the default gate
}

TEST_CASE("A bank without a slicer object keeps the default gate", "[groove][json]") {
    const auto bank = parseGrooveBank(R"({"scenes":[]})");
    REQUIRE(bank.has_value());
    CHECK(bank->slicerSteps[3].on());
    CHECK(bank->slicerLength == 16);
}

TEST_CASE("Edits are strict about where they point", "[groove][json]") {
    std::string error;
    const char* head = R"({"format":3,"lanes":{"hat":{"groove":"x..............."}},"edits":[)";

    CHECK_FALSE(parsePattern(std::string(head) + R"({"lane":"hat","step":16,"ratchet":2}]})", &error));
    CHECK(error.find("step") != std::string::npos);
    CHECK_FALSE(parsePattern(std::string(head) + R"({"lane":"hat","step":-1,"ratchet":2}]})", &error));
    CHECK_FALSE(parsePattern(std::string(head) + R"({"lane":"hat","ratchet":2}]})", &error));
    CHECK_FALSE(parsePattern(std::string(head) + R"({"lane":"cowbell","step":0}]})", &error));
    CHECK_FALSE(parsePattern(std::string(head) + R"(7]})", &error));

    // An edit may not switch on a step that has no character.
    CHECK_FALSE(parsePattern(std::string(head) + R"({"lane":"hat","step":4,"velocity":90}]})", &error));
    CHECK(error.find("off") != std::string::npos);

    // A fill edit before the fill window is ignored, as the spec says.
    CHECK(parsePattern(std::string(head) + R"({"lane":"hat","step":2,"fill":true,"ratchet":2}]})", &error));

    CHECK_FALSE(parseGrooveBank(R"({"slicer":{"length":8,"steps":"x.......","edits":[{"step":8,"ratchet":2}]}})", &error));
    CHECK_FALSE(parseGrooveBank(R"({"slicer":{"length":8,"steps":"x.......","edits":[{"step":1,"ratchet":2}]}})", &error));
    CHECK_FALSE(parseGrooveBank(R"({"slicer":{"length":8,"steps":"x.......","edits":[{"ratchet":2}]}})", &error));
    CHECK_FALSE(parseGrooveBank(R"({"slicer":{"length":8,"steps":"x.......","edits":[3]}})", &error));
}

TEST_CASE("Any sanitised bank survives a round trip unchanged", "[groove][json]") {
    std::uint32_t state = 20261126u;
    const auto next = [&state](int lo, int hi) {
        state = state * 1664525u + 1013904223u;
        return lo + static_cast<int>((state >> 8) % static_cast<std::uint32_t>(hi - lo + 1));
    };
    const auto randomStep = [&next](Step& s) {
        if (next(0, 3) != 0)
            return;
        s.velocity = static_cast<std::uint8_t>(next(1, 127));
        s.layer = static_cast<Layer>(next(0, 2));
        s.ratchet = static_cast<std::uint8_t>(next(1, 4));
        s.nudge = static_cast<std::int8_t>(next(-24, 24));
        s.flam = next(0, 4) == 0;
    };
    for (int round = 0; round < 20; ++round) {
        GrooveBank bank;
        for (auto& p : bank.scenes) {
            p.bars = static_cast<std::uint8_t>(1 << next(0, 2));
            p.fill.beats = static_cast<std::uint8_t>(1 << next(0, 2));
            for (auto& lane : p.lanes) {
                for (auto& s : lane.steps)
                    randomStep(s);
                lane.length = static_cast<std::uint8_t>(next(0, 64));
                lane.midiNote = static_cast<std::uint8_t>(next(0, 127));
                lane.offsetMs = static_cast<std::int8_t>(next(-20, 20));
                lane.muted = next(0, 5) == 0;
                lane.solo = next(0, 9) == 0;
            }
            for (auto& row : p.fill.steps)
                for (auto& s : row)
                    randomStep(s);
        }
        bank.slicerLength = static_cast<std::uint8_t>(next(1, 32));
        for (auto& s : bank.slicerSteps)
            randomStep(s);
        sanitise(bank);

        std::string error;
        const auto back = parseGrooveBank(serialiseGrooveBank(bank), &error);
        INFO("round " << round << ": " << error);
        REQUIRE(back.has_value());
        CHECK(*back == bank);
    }
}
```

Add `#include <cstdint>` to that file's includes.

- [x] **Step 6: Make the reader strict and sanitised**

In `core/src/groove_json.cpp`:

Add a second helper directly below `intField`:

```cpp
// Like intField, but a value outside lo..hi is a failure instead of being clamped.
bool intInRange(const Json& obj, const char* key, int lo, int hi, int& out) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer())
        return false;
    const int v = it->get<int>();
    if (v < lo || v > hi)
        return false;
    out = v;
    return true;
}
```

Replace the whole `if (value.contains("edits") && value["edits"].is_array()) { ... }` block in `readPattern`, and the fill-clearing loop after it, with:

```cpp
    if (value.contains("edits") && value["edits"].is_array()) {
        const int fillFrom = fillWindowStart(p.fill);
        for (const auto& edit : value["edits"]) {
            if (!edit.is_object()) {
                fail(error, "every edit must be an object");
                return std::nullopt;
            }
            const int lane = laneOfKey(hba::core::jsonStringOr(edit, "lane"));
            const bool inFill = hba::core::jsonBoolOr(edit, "fill");
            int step = 0;
            if (lane < 0 || !intInRange(edit, "step", 0, (inFill ? kStepsPerBar : steps) - 1, step)) {
                fail(error, "edit needs a known \"lane\" and a \"step\" inside the pattern");
                return std::nullopt;
            }
            if (inFill && step < fillFrom)
                continue; // only the fill window is part of the fill
            const auto li = static_cast<std::size_t>(lane);
            const auto si = static_cast<std::size_t>(step);
            Step& target = inFill ? p.fill.steps[li][si] : p.lanes[li].steps[si];
            if (!target.on()) {
                fail(error, "edit targets a step that is off");
                return std::nullopt;
            }
            applyEdit(edit, target);
        }
    }
    sanitise(p);
    return p;
```

(The old fill-clearing loop is no longer needed: `sanitise` clears fill steps before the window.)

In `readGrooveBank`, replace the `if (value.contains("slicer") && value["slicer"].is_object()) { ... }` block and the `return bank;` after it with:

```cpp
    if (value.contains("slicer") && value["slicer"].is_object()) {
        const Json& so = value["slicer"];
        int v = 0;
        if (intField(so, "length", 1, kMaxSlicerSteps, v))
            bank.slicerLength = static_cast<std::uint8_t>(v);
        if (so.contains("steps")) {
            bank.slicerSteps = {}; // the file's strip replaces the default gate
            if (!readSteps(so["steps"], bank.slicerSteps.data(), kMaxSlicerSteps, false, false, Layer::groove,
                           "slicer.steps", error))
                return std::nullopt;
        }
        if (so.contains("edits") && so["edits"].is_array()) {
            for (const auto& edit : so["edits"]) {
                int step = 0;
                if (!edit.is_object() ||
                    !intInRange(edit, "step", 0, static_cast<int>(bank.slicerLength) - 1, step)) {
                    fail(error, "slicer edit needs a \"step\" inside the slicer length");
                    return std::nullopt;
                }
                Step& target = bank.slicerSteps[static_cast<std::size_t>(step)];
                if (!target.on()) {
                    fail(error, "edit targets a step that is off");
                    return std::nullopt;
                }
                applyEdit(edit, target);
            }
        }
    }
    sanitise(bank);
    return bank;
```

- [x] **Step 7: Run the core tests**

```bash
cmake --build --preset engine --target cl-core-tests && build/engine/tests/cl-core-tests
```

Expected: `All tests passed`. If "Patterns and banks round-trip with every field set" fails, the bank that test builds is not in canonical form (for example an off step with a ratchet, or a step beyond its pattern length). Add `sanitise(bank);` before that test serialises, and `sanitise(p);` before it serialises a pattern. Do not weaken its assertions.

- [x] **Step 8: Write the failing resolve and timing tests**

Append to `tests/engine/ResolveTests.cpp` (add `#include <cmath>` and `#include <limits>`):

```cpp
TEST_CASE("Bar 2 of a pattern resolves its own steps, and a negative bar wraps", "[engine][resolve]") {
    Pattern p{};
    p.bars = 2;
    p.lanes[0].steps[0] = hit(96, Layer::core);
    p.lanes[1].steps[20] = hit(96, Layer::core); // bar 2, step 4
    ResolvedBar bar;
    resolveBar(p, 1, {0.5f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].velocity == 0);
    CHECK(bar.lanes[1][4].velocity == 96);
    resolveBar(p, -1, {0.5f, 0.5f, false}, bar); // the bar before bar 0 is the last bar
    CHECK(bar.lanes[1][4].velocity == 96);
    resolveBar(p, -2, {0.5f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].velocity == 96);
}

TEST_CASE("A looped lane keeps its phase across the bar line", "[engine][resolve]") {
    Pattern p{};
    p.bars = 2;
    p.lanes[6].length = 5;
    p.lanes[6].steps[0] = hit(96, Layer::core);
    ResolvedBar bar;
    resolveBar(p, 0, {0.5f, 0.5f, false}, bar);
    for (int step = 0; step < kStepsPerBar; ++step)
        CHECK((bar.lanes[6][static_cast<std::size_t>(step)].velocity > 0) == (step % 5 == 0));
    resolveBar(p, 1, {0.5f, 0.5f, false}, bar);
    for (int step = 0; step < kStepsPerBar; ++step)
        CHECK((bar.lanes[6][static_cast<std::size_t>(step)].velocity > 0) == ((16 + step) % 5 == 0));
}

TEST_CASE("At one metric weight the lanes arrive kick, snare, hat, open hat, clap, ghost, perc A, perc B",
          "[engine][resolve]") {
    const std::array<int, kDrumLaneCount> order{0, 1, 4, 5, 3, 2, 6, 7};
    for (std::size_t i = 0; i + 1 < order.size(); ++i) {
        CHECK(rankBefore(order[i], 4, order[i + 1], 4));
        CHECK_FALSE(rankBefore(order[i + 1], 4, order[i], 4));
    }
    Pattern p{};
    for (int lane = 0; lane < kDrumLaneCount; ++lane)
        p.lanes[static_cast<std::size_t>(lane)].steps[4] = hit(96, Layer::groove);
    ResolvedBar bar;
    for (std::size_t n = 1; n <= order.size(); ++n) {
        resolveBar(p, 0, {0.5f * static_cast<float>(n) / 8.0f, 0.5f, false}, bar);
        for (std::size_t i = 0; i < order.size(); ++i)
            CHECK((bar.lanes[static_cast<std::size_t>(order[i])][4].velocity > 0) == (i < n));
    }
}

TEST_CASE("A lane that is both soloed and muted is silent, and mute silences fill steps", "[engine][resolve]") {
    Pattern p{};
    p.lanes[0].steps[0] = hit(96, Layer::core);
    p.lanes[1].steps[4] = hit(96, Layer::core);
    p.lanes[0].solo = true;
    p.lanes[0].muted = true;
    ResolvedBar bar;
    resolveBar(p, 0, {0.5f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].written);
    CHECK(bar.lanes[0][0].velocity == 0);
    CHECK(bar.lanes[1][4].velocity == 0); // another lane is soloed

    Pattern f{};
    f.fill.beats = 1;
    f.fill.steps[1][12] = hit(96, Layer::groove);
    f.lanes[1].muted = true;
    resolveBar(f, 0, {0.5f, 0.5f, true}, bar);
    CHECK(bar.lanes[1][12].written);
    CHECK(bar.lanes[1][12].fromFill);
    CHECK(bar.lanes[1][12].velocity == 0);
}

TEST_CASE("Resolve clamps ratchet and nudge and survives NaN controls", "[engine][resolve]") {
    Pattern p{};
    p.lanes[0].steps[0] = hit(96, Layer::core);
    p.lanes[0].steps[0].ratchet = 9;
    p.lanes[0].steps[0].nudge = 100;
    p.lanes[4].steps[2] = hit(96, Layer::groove);
    ResolvedBar bar;
    resolveBar(p, 0, {0.5f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].ratchet == 4);
    CHECK(bar.lanes[0][0].nudge == 24);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    resolveBar(p, 0, {nan, nan, false}, bar);
    CHECK(bar.lanes[0][0].velocity == 96); // Core sounds, at the identity Dynamics
    CHECK(bar.lanes[4][2].written);
    CHECK(bar.lanes[4][2].velocity == 0); // a NaN Density is 0%: Core only
    CHECK(applyDynamics(96, nan) == 96);
}
```

Add `#include <array>` to that file if it is not already there.

Append to `tests/engine/HitsTests.cpp`:

```cpp
TEST_CASE("A flam's grace hit never drops below velocity 1", "[engine][hits]") {
    Pattern p{};
    p.lanes[1].steps[4] = hit(20);
    p.lanes[1].steps[4].flam = true;
    const auto hits = hitsOf(p, 0.0, {0.0, 0.6, 172.0 / 60000.0});
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].grace);
    CHECK(hits[0].velocity == 1);
}

TEST_CASE("Open hat and perc B ring longer than the gated lanes", "[engine][hits]") {
    CHECK(isLongNoteLane(kOpenHatLane));
    CHECK(isLongNoteLane(kPercBLane));
    CHECK_FALSE(isLongNoteLane(4));
    Pattern p{};
    for (int lane = 0; lane < kDrumLaneCount; ++lane)
        p.lanes[static_cast<std::size_t>(lane)].steps[0] = hit(96);
    const auto hits = hitsOf(p, 0.0, {0.0, 0.0, 172.0 / 60000.0});
    REQUIRE(hits.size() == 8);
    for (const auto& h : hits)
        CHECK(h.durBeats == Approx(isLongNoteLane(h.lane) ? 0.36 : 0.12));
}

TEST_CASE("Lane offset is clamped to 20 ms on the play path", "[engine][hits]") {
    const double beatsPerMs = 120.0 / 60000.0;
    Pattern p{};
    p.lanes[0].steps[4] = hit(96);
    p.lanes[0].offsetMs = 90; // out of range in memory
    const auto hits = hitsOf(p, 0.0, {0.0, 0.6, beatsPerMs});
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].beat == Approx(1.0 + 20.0 * beatsPerMs));
}

TEST_CASE("No hit starts earlier or later than the published bounds", "[engine][hits]") {
    const double beatsPerMs = 96.0 / 60000.0;
    Pattern p{};
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        p.lanes[static_cast<std::size_t>(lane)].offsetMs = static_cast<std::int8_t>(lane % 2 == 0 ? -20 : 20);
        for (int step = 0; step < kStepsPerBar; ++step) {
            Step& s = p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
            s = hit(96);
            s.nudge = static_cast<std::int8_t>(lane % 2 == 0 ? -24 : 24);
            s.flam = true;
            s.ratchet = 4;
        }
    }
    for (const double swing : {0.0, 0.67, 1.0}) {
        const auto hits = hitsOf(p, 8.0, {swing, 0.6, beatsPerMs});
        for (const auto& h : hits) {
            const double stepStart = 8.0 + static_cast<double>(h.step) * 0.25;
            CHECK(h.beat >= stepStart - maxEarlyBeats(beatsPerMs) - 1.0e-9);
            CHECK(h.beat < stepStart + 0.25 + maxLateBeats(beatsPerMs));
        }
    }
}
```

- [x] **Step 9: Run them to verify they fail**

```bash
cmake --build --preset engine --target cl-engine-tests
```

Expected: the build fails with `use of undeclared identifier 'isLongNoteLane'`.

- [x] **Step 10: Add the timing helpers and the guards**

In `engine/include/cl/hits.h`, add `#include <cstddef>` and, after `kFlamVelocityDrop`, add:

```cpp
inline constexpr int kOpenHatLane = 5;
inline constexpr int kPercBLane = 7;
inline constexpr int kMaxNudge = 24;    // 1/96 beats
inline constexpr int kMaxOffsetMs = 20; // whole-lane pocket

// Open hat and perc B ring for a fixed length; the other lanes follow Gate.
constexpr bool isLongNoteLane(int lane) noexcept CL_NONBLOCKING { return lane == kOpenHatLane || lane == kPercBLane; }

// How far before its step a hit can start: a full negative nudge, a full negative lane offset and the flam.
inline double maxEarlyBeats(double beatsPerMs) noexcept CL_NONBLOCKING {
    return static_cast<double>(kMaxNudge) / 96.0 + (static_cast<double>(kMaxOffsetMs) + kFlamMs) * beatsPerMs;
}

// How far after the end of its step a hit can start: a full positive nudge and a full positive lane offset.
inline double maxLateBeats(double beatsPerMs) noexcept CL_NONBLOCKING {
    return static_cast<double>(kMaxNudge) / 96.0 + static_cast<double>(kMaxOffsetMs) * beatsPerMs;
}
```

In `forEachHit`, replace the two lines that compute `offsetBeats` and `baseDur` with:

```cpp
        const int offsetMs = std::clamp(static_cast<int>(pattern.lanes[li].offsetMs), -kMaxOffsetMs, kMaxOffsetMs);
        const double offsetBeats = static_cast<double>(offsetMs) * timing.beatsPerMs;
        const double baseDur = isLongNoteLane(lane) ? 0.36 : 0.12 + 0.14 * std::clamp(timing.gateLen, 0.0, 1.0);
```

In `engine/src/resolve.cpp`, add `#include <cstddef>`. Replace `applyDynamics` with:

```cpp
std::uint8_t applyDynamics(int velocity, float dynamics) noexcept CL_NONBLOCKING {
    const float d = std::isfinite(dynamics) ? std::clamp(dynamics, 0.0f, 1.0f) : 0.5f;
    const float v = 64.0f + (static_cast<float>(velocity) - 64.0f) * (0.5f + d);
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 1, 127));
}
```

In the cached `resolveBar`, replace the line that computes `density` with:

```cpp
    // A non-finite Density plays Core only. The epsilon is there because thresholds are computed as
    // 0.5f * i / n: a Density typed as the same fraction can land one float step below its threshold.
    const float rawDensity = std::isfinite(in.density) ? in.density : 0.0f;
    const float density = std::clamp(rawDensity, 0.0f, 1.0f) + 1.0e-6f;
```

- [x] **Step 11: Run the whole engine preset**

```bash
cmake --build --preset engine && ctest --preset engine 2>&1 | tail -3
```

Expected: `100% tests passed, 0 tests failed out of 56`. (40 before, plus 3 model, 4 JSON, 5 resolve and 4 timing test cases.)

- [x] **Step 12: Check formatting and commit**

```bash
xcrun clang-format --style=file:../../.clang-format --dry-run --Werror \
  core/include/cl/groove.h core/src/groove_json.cpp engine/include/cl/hits.h engine/src/resolve.cpp \
  tests/core/GrooveTests.cpp tests/core/GrooveJsonTests.cpp tests/engine/ResolveTests.cpp tests/engine/HitsTests.cpp
```

Expected: no output. If it reports a file, run the same command with `-i` in place of `--dry-run --Werror` and rebuild.

```bash
git add core/include/cl/groove.h core/src/groove_json.cpp engine/include/cl/hits.h engine/src/resolve.cpp tests/core tests/engine
git commit -m "feat(cutline): sanitise groove banks, tighten edits and guard resolve inputs"
```

---

### Task 2: Parameter contract and presets

From this task until Task 4 ends, `cmake --build --preset engine` fails as a whole, and `dev` fails until Task 6 ends. That is expected. Build only the targets named here.

**Files:**
- Rewrite: `core/include/cl/params.h`
- Rewrite: `core/include/cl/pattern.h`, `core/src/pattern.cpp`
- Rewrite: `core/include/cl/preset.h`, `core/src/preset.cpp`
- Rewrite: `tests/core/CoreTests.cpp`
- Create: `tests/core/PresetTests.cpp`
- Modify: `tests/CMakeLists.txt` (the `cl-core-tests` source list)

**Interfaces:**
- Consumes: `GrooveBank`, `sanitise` (Task 1); `detail::readGrooveBank`, `detail::writeGrooveBank`, `kGrooveFormat`, `kOldFormatError` (Plan 1).
- Produces, in `cl/params.h`: `enum class ParamId` in the order below; `kParamCount == 42`; `kPerfPadCount == 4`; `enum class PadProgram : std::uint8_t { repeat = 0, roll = 1, rate = 2, drop = 3 }`; `choices::fillEvery`. Removed: `ParamId::style`, `gravity`, `ghosts`, `chop`, `ratchet`, `complexity`, `fills`, `phraseBars`, `attractor`, `fillType`, `variation`; `enum DrumStyle`, `Attractor`, `FillType`; `kDrumStyleCount`, `kPhraseBarCounts`; `choices::style`, `phraseBars`, `attractor`, `fillType`.
- Produces, in `cl/pattern.h`: `kDrumLaneCount`, `kDrumLaneNames`, `kKitNoteMaps`, `kKitLaneNames`, `kitLaneName`, `kPerfPadLabels`, `kPerfPadXLabels`, `kPerfPadYLabels`, `kPerfPadHints` (four entries each), `std::int8_t effectiveLaneMidiNote(NoteMap map, int lane, int overrideNote) noexcept CL_NONBLOCKING`, `std::string midiNoteLabel(int midiNote)`. Removed: `kMaxLaneSteps`, `StepState`, `StepEdit`, `LaneConfig`, `SceneData`, `PatternBank`, `effectiveSceneData`, `mutateUnlockedLanes`, `applyPolymeterToUnlocked`, `cloneSceneSlot`, `clearSceneEdits`.
- Produces, in `cl/preset.h`: `struct Preset { std::string name; std::string category; Params params; LaneCurves curves = defaultLaneCurves(); GrooveBank bank{}; }`; `std::optional<Preset> parsePreset(std::string_view json, std::string* error = nullptr)`; `std::string serialisePreset(const Preset&)`; `parseCurve`, `serialiseCurve` unchanged. Removed: `parsePatternBank`, `serialisePatternBank` (use `parseGrooveBank` and `serialiseGrooveBank` from `cl/groove_json.h`).

Parameter order (index, key, name, kind, range, default, automatable):

| # | Key | Name | Kind | Range | Default | Automatable |
|---|-----|------|------|-------|---------|-------------|
| 1 | `density` | Density | amount | 0..1 | 0.5 | yes |
| 2 | `swing` | Swing | amount | 0..1 | 0 | yes |
| 3 | `dynamics` | Dynamics | amount | 0..1 | 0.5 | yes |
| 4 | `fillEvery` | Fill | choice | Off, 2 bars, 4 bars, 8 bars, 16 bars | Off | yes |
| 5 | `scene` | Scene | choice | A-D | A | yes |
| 6 | `seqRate` | Rate | choice | 0.5x, 1x, 2x | 1x | yes |
| 7 | `padX` | Pad X | amount | 0..1 | 0.5 | yes |
| 8 | `padY` | Pad Y | amount | 0..1 | 0.5 | yes |
| 9 | `fillNow` | Fill now | toggle | | Off | yes |
| 10 | `engineMode` | Mode | choice | Drum Engine, MIDI Slicer | Drum Engine | no |
| 11 | `noteMap` | Kit map | choice | 10 maps | Live Rack / GM | no |
| 12 | `slicerStyle` | Slicer | choice | 5 styles | Stab Chop | yes |
| 13 | `sceneQuantize` | Quant | choice | Instant, 1/16, 1/4, 1 Bar | 1 Bar | no |
| 14 | `padMidi` | Pad MIDI | toggle | | Off | no |
| 15 | `slicerLatch` | Hold MIDI | toggle | | Off | no |
| 16 | `strumMs` | Strum | ranged ms | 0..45 | 14 | yes |
| 17 | `gateLen` | Gate | amount | 0..1 | 0.6 | yes |
| 18 | `shiftMs` | Shift | ranged ms | -25..25 | 0 | yes |
| 19 | `audition` | Audition | toggle | | On | no |
| 20 | `auditionGain` | Aud gain | ranged dB | -36..0 | -6 | yes |
| 21 | `motion` | Expr amt | amount | 0..1 | 0.45 | yes |
| 22 | `padProgram` | Pad mode | choice | Repeat, Roll, Rate, Drop | Repeat | yes |
| 23 | `padTouch` | Pad touch | toggle | | Off | yes |
| 24 | `padHold` | Hold | toggle | | Off | yes |
| 25 | `padCcMode` | XY → CC | choice | Off, CC74 + CC1, CC74 + Vel, CC1 + AT | Off | no |
| 26-33 | `lane1On` .. `lane1Ratio` | Lane 1 | as in 2.0 | | lane 1 Off, CC74 Slide, rate index 2, depth 0.48 | as in 2.0 |
| 34-41 | `lane2On` .. `lane2Ratio` | Lane 2 | as in 2.0 | | Off, Velocity, rate index 3, depth 0.36 | as in 2.0 |
| 42 | `ccThinMs` | CC rate | choice | 1, 2, 4, 8 ms | 2 ms | no |

- [x] **Step 1: Write the failing tests**

Replace `tests/core/CoreTests.cpp` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/pattern.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <set>
#include <string>
#include <string_view>

using namespace cl;
using Catch::Approx;

TEST_CASE("The parameter contract has 42 unique keys that round-trip", "[core]") {
    REQUIRE(kParamCount == 42);
    REQUIRE(kParams.size() == static_cast<std::size_t>(kParamCount));

    std::set<std::string> keys;
    for (int i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        const auto& s = spec(id);
        REQUIRE(!s.key.empty());
        REQUIRE(keys.insert(std::string(s.key)).second);
        REQUIRE(findParam(s.key) == id);
        const float norm = toNormalised(s, s.def);
        CHECK(norm >= 0.0f);
        CHECK(norm <= 1.0f);
        CHECK(fromNormalised(s, norm) == Approx(s.def).margin(1e-3f));
        CHECK(!formatValue(s, s.def).empty());
    }
}

TEST_CASE("Bank 1 is Density, Swing, Dynamics, Fill, Scene, Rate, Pad X, Pad Y", "[core]") {
    const std::array<std::string_view, 8> keys{"density", "swing", "dynamics", "fillEvery",
                                                "scene",   "seqRate", "padX",  "padY"};
    for (std::size_t i = 0; i < keys.size(); ++i)
        CHECK(kParams[i].key == keys[i]);
    CHECK(kParams[8].key == "fillNow");
    CHECK(kParams[static_cast<std::size_t>(kParamCount) - 1].key == "ccThinMs");

    const Params p;
    CHECK(p[ParamId::density] == Approx(0.5f));
    CHECK(p[ParamId::swing] == Approx(0.0f));
    CHECK(p[ParamId::dynamics] == Approx(0.5f));
    CHECK(p.choice(ParamId::fillEvery) == 0);
    CHECK(formatValue(spec(ParamId::fillEvery), 2.0f) == "4 bars");
    CHECK_FALSE(p.on(ParamId::fillNow));
    CHECK(p[ParamId::padX] == Approx(0.5f));
    CHECK(p[ParamId::padY] == Approx(0.5f));
}

TEST_CASE("Nothing sends MIDI the user did not ask for by default", "[core]") {
    const Params p;
    CHECK_FALSE(p.on(ParamId::padMidi));
    CHECK(p.choice(ParamId::padCcMode) == static_cast<int>(PadCcMode::off));
    CHECK_FALSE(p.on(ParamId::lane1On));
    CHECK_FALSE(p.on(ParamId::lane2On));
    CHECK(p.choice(ParamId::sceneQuantize) == static_cast<int>(Quantize::oneBar));
    CHECK(p.choice(ParamId::padProgram) == static_cast<int>(PadProgram::repeat));
}

TEST_CASE("The Cutline 2 generator parameters are gone", "[core]") {
    for (const char* key : {"style", "gravity", "ghosts", "chop", "ratchet", "complexity", "fills", "phraseBars",
                            "attractor", "fillType", "variation"})
        CHECK_FALSE(findParam(key).has_value());
    CHECK(kPerfPadCount == 4);
    CHECK(spec(ParamId::padProgram).choices.size() == 4);
    CHECK(kPerfPadLabels[0] == "Repeat");
    CHECK(kPerfPadLabels[3] == "Drop");
}

TEST_CASE("Kit note maps resolve built-in notes and per-lane overrides", "[core]") {
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 0, 0) == 36);
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 1, 0) == 38);
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 4, 0) == 42);
    CHECK(effectiveLaneMidiNote(NoteMap::strikeBody, 1, 0) == 36);
    CHECK(effectiveLaneMidiNote(NoteMap::strikeBody, 6, 0) == 39);
    CHECK(effectiveLaneMidiNote(NoteMap::chromatic, 7, 0) == 43);
    CHECK(effectiveLaneMidiNote(NoteMap::addictiveDrums, 5, 0) == 55);
    CHECK(effectiveLaneMidiNote(NoteMap::bfd3, 0, 0) == 24);
    CHECK(effectiveLaneMidiNote(NoteMap::xlnXo, 7, 0) == 43);
    CHECK(effectiveLaneMidiNote(NoteMap::addictiveDrums, 0, 51) == 51);
    CHECK(effectiveLaneMidiNote(NoteMap::liveRack, 0, 200) == 36); // out of range: fall back to the map

    for (int m = 0; m < kNoteMapCount; ++m) {
        for (int l = 0; l < kDrumLaneCount; ++l) {
            const int note = effectiveLaneMidiNote(static_cast<NoteMap>(m), l, 0);
            CHECK(note >= 12);
            CHECK(note <= 120);
            CHECK(!kitLaneName(static_cast<NoteMap>(m), l).empty());
        }
    }
    CHECK(midiNoteLabel(60) == "C3");
}
```

If `midiNoteLabel(60)` is not `"C3"` in this codebase, change that one expectation to whatever `hba::core::midiNoteName(60)` returns today; the function is unchanged.

Create `tests/core/PresetTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove_json.h"
#include "cl/preset.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cl;
using Catch::Approx;

TEST_CASE("A preset round-trips its parameters, curves and bank", "[preset]") {
    Preset orig;
    orig.name = "Two-Step";
    orig.category = "Club";
    orig.params[ParamId::density] = 0.72f;
    orig.params[ParamId::swing] = 0.44f;
    orig.params[ParamId::fillEvery] = 2.0f;
    orig.params[ParamId::noteMap] = static_cast<float>(NoteMap::strikeBody);
    orig.curves[1] = curves::sine();
    orig.bank.scenes[1].bars = 2;
    orig.bank.scenes[1].lanes[6].length = 7;
    orig.bank.scenes[1].lanes[6].steps[2].velocity = 71;
    orig.bank.scenes[1].lanes[6].steps[2].flam = true;
    orig.bank.scenes[1].fill.beats = 1;
    orig.bank.scenes[1].fill.steps[1][14].velocity = 118;
    orig.bank.slicerLength = 12;
    orig.bank.slicerSteps[3].ratchet = 3;
    sanitise(orig.bank);

    const std::string json = serialisePreset(orig);
    CHECK(json.find("\"format\": 3") != std::string::npos);

    std::string error;
    const auto parsed = parsePreset(json, &error);
    INFO(error);
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == "Two-Step");
    CHECK(parsed->category == "Club");
    CHECK(parsed->params[ParamId::density] == Approx(0.72f).margin(1e-3f));
    CHECK(parsed->params[ParamId::swing] == Approx(0.44f).margin(1e-3f));
    CHECK(parsed->params.choice(ParamId::fillEvery) == 2);
    CHECK(parsed->params.choice(ParamId::noteMap) == static_cast<int>(NoteMap::strikeBody));
    CHECK(parsed->curves[1] == orig.curves[1]);
    CHECK(parsed->bank == orig.bank);
}

TEST_CASE("A Cutline 2 preset is rejected with a clear message", "[preset]") {
    std::string error;
    CHECK_FALSE(parsePreset(R"({"name":"Amen Chop 172","category":"Breaks","style":"Amen","gravity":0.24})", &error));
    CHECK(error == kOldFormatError);
    CHECK_FALSE(parsePreset(R"({"format":2,"name":"Old"})", &error));
    CHECK(error == kOldFormatError);
    CHECK_FALSE(parsePreset(R"({"format":"3","name":"Old"})", &error));
    CHECK(error == kOldFormatError);
}

TEST_CASE("A Cutline 3 preset is strict about what it contains", "[preset]") {
    std::string error;
    CHECK_FALSE(parsePreset("not json", &error));
    CHECK_FALSE(parsePreset(R"({"format":3,"gravity":0.2})", &error));
    CHECK(error.find("gravity") != std::string::npos);
    CHECK_FALSE(parsePreset(R"({"format":3,"density":"loud"})", &error));
    CHECK_FALSE(parsePreset(R"({"format":3,"bank":{"scenes":[{"bars":3}]}})", &error));
    CHECK(error.find("bars") != std::string::npos);
}

TEST_CASE("A preset without a bank gets the default bank", "[preset]") {
    const auto parsed = parsePreset(R"({"format":3,"name":"Bare","swing":0.2})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->bank == GrooveBank{});
    CHECK(parsed->params[ParamId::swing] == Approx(0.2f));
}
```

In `tests/CMakeLists.txt`, change line 1 to:

```cmake
add_executable(cl-core-tests core/CoreTests.cpp core/GrooveTests.cpp core/GrooveJsonTests.cpp core/PresetTests.cpp)
```

- [x] **Step 2: Run them to verify they fail**

```bash
cmake --build --preset engine --target cl-core-tests
```

Expected: the build fails; the first error names `ParamId::density` (`no member named 'density' in 'cl::ParamId'`).

- [x] **Step 3: Rewrite `params.h`**

In `core/include/cl/params.h`, replace the header comment, the `ParamId` enum, `kPerfPadCount`, the style, attractor, fill-type and pad-program enums, the `choices` namespace, the two rate tables, the `CL_LANE` invocations and the `kParams` table. Everything not shown here (`LaneField`, `LaneTarget`, `laneParam`, `EngineMode`, `NoteMap`, `SlicerStyle`, `SeqRate`, `Quantize`, `PadCcMode`, `LaneShape`, `ParamKind`, `Unit`, `ParamSpec`, `namespace detail`, the `CL_LANE` macro body, `spec`, `Params`, and the function declarations) stays as it is.

Header comment:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Single source of truth for Cutline 3's parameter contract: 42 parameters. Order is the automation
// contract; the first eight are what Ableton Live and Push show as bank 1.
```

The enum:

```cpp
enum class ParamId : std::uint8_t {
    // Bank 1
    density,
    swing,
    dynamics,
    fillEvery,
    scene,
    seqRate, // 0.5x, 1x, 2x
    padX,
    padY,

    fillNow,
    engineMode,
    noteMap,
    slicerStyle,
    sceneQuantize,
    padMidi,
    slicerLatch,
    strumMs,
    gateLen,
    shiftMs,
    audition,
    auditionGain,
    motion,

    // Interim pad surface
    padProgram, // 0..3: Repeat, Roll, Rate, Drop
    padTouch,
    padHold,
    padCcMode,

    // Two optional tempo-locked expression lanes
    lane1On,
    lane1Rate,
    lane1Phase,
    lane1Depth,
    lane1Shape,
    lane1Target,
    lane1Follow,
    lane1Ratio,
    lane2On,
    lane2Rate,
    lane2Phase,
    lane2Depth,
    lane2Shape,
    lane2Target,
    lane2Follow,
    lane2Ratio,

    ccThinMs,
    count
};
```

Constants and enums (delete `DrumStyle`, `kDrumStyleCount`, `Attractor`, `FillType`, and replace `PadProgram`):

```cpp
inline constexpr int kLaneCount = 2;
inline constexpr int kPerfPadCount = 4;
inline constexpr int kSceneCount = 4;
```

```cpp
enum class PadProgram : std::uint8_t { repeat = 0, roll = 1, rate = 2, drop = 3 };
```

In `namespace choices`, delete `style`, `phraseBars`, `attractor` and `fillType`, replace `padProgram`, and add `fillEvery`:

```cpp
inline constexpr std::array<std::string_view, 5> fillEvery{"Off", "2 bars", "4 bars", "8 bars", "16 bars"};
inline constexpr std::array<std::string_view, 4> padProgram{"Repeat", "Roll", "Rate", "Drop"};
```

Delete `kPhraseBarCounts`. Keep `kSeqRateMultipliers`, `kCcThinMsValues`, `kLaneRateQuarters`, `kLaneRatios`.

The table:

```cpp
// Order must match ParamId.
inline constexpr std::array<ParamSpec, kParamCount> kParams{{
    // Bank 1
    detail::amount("density", "Density", 0.50f),
    detail::amount("swing", "Swing", 0.0f),
    detail::amount("dynamics", "Dynamics", 0.50f),
    detail::choice("fillEvery", "Fill", choices::fillEvery, 0, true),
    detail::choice("scene", "Scene", choices::scene, 0, true),
    detail::choice("seqRate", "Rate", choices::seqRate, 1, true),
    detail::amount("padX", "Pad X", 0.50f),
    detail::amount("padY", "Pad Y", 0.50f),

    detail::toggle("fillNow", "Fill now", false, true),
    detail::choice("engineMode", "Mode", choices::engineMode, 0, false),
    detail::choice("noteMap", "Kit map", choices::noteMap, 0, false),
    detail::choice("slicerStyle", "Slicer", choices::slicerStyle, 0, true),
    detail::choice("sceneQuantize", "Quant", choices::sceneQuantize, 3, false),
    detail::toggle("padMidi", "Pad MIDI", false, false),
    detail::toggle("slicerLatch", "Hold MIDI", false, false),
    detail::ranged("strumMs", "Strum", 0.0f, 45.0f, 14.0f, Unit::ms),
    detail::amount("gateLen", "Gate", 0.60f),
    detail::ranged("shiftMs", "Shift", -25.0f, 25.0f, 0.0f, Unit::ms),
    detail::toggle("audition", "Audition", true, false),
    detail::ranged("auditionGain", "Aud gain", -36.0f, 0.0f, -6.0f, Unit::dB),
    detail::amount("motion", "Expr amt", 0.45f),

    detail::choice("padProgram", "Pad mode", choices::padProgram, 0, true),
    detail::toggle("padTouch", "Pad touch", false, true),
    detail::toggle("padHold", "Hold", false, true),
    detail::choice("padCcMode", "XY → CC", choices::padCcMode, 0, false),

    CL_LANE(1, false, LaneTarget::cc74Slide, 2, 0.0f, 0.48f),
    CL_LANE(2, false, LaneTarget::velocity, 3, 0.0f, 0.36f),

    detail::choice("ccThinMs", "CC rate", choices::ccThinMs, 1, false),
}};
```

- [x] **Step 4: Rewrite `pattern.h` and `pattern.cpp`**

In `core/include/cl/pattern.h`: keep the includes (drop `<cmath>` if nothing uses it), `kDrumLaneCount`, `kDrumLaneNames`, the kit-map comment, `kKitNoteMaps`, `kKitLaneNames` and `kitLaneName` exactly as they are. Delete `kMaxLaneSteps` and everything from `enum class StepState` to the end of the file except the closing namespace. Replace the header comment and the four pad label tables, and add the two declarations:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Drum lanes, kit note maps and pad labels. The pattern data itself lives in cl/groove.h.
```

```cpp
inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadLabels{"Repeat", "Roll", "Rate", "Drop"};
inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadXLabels{"Window", "Rate", "Speed", "Cut ↔ Fill"};
inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadYLabels{"Gate", "Velocity", "Kick ↔ Tops", ""};
inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadHints{"C3", "C#3", "D3", "D#3"};
```

```cpp
// The MIDI note a drum lane plays: its override when that is 1..127, otherwise the kit map's note.
std::int8_t effectiveLaneMidiNote(NoteMap map, int lane, int overrideNote) noexcept CL_NONBLOCKING;

std::string midiNoteLabel(int midiNote);
```

Replace `core/src/pattern.cpp` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/pattern.h"

#include <hba/core/note_name.h>

#include <algorithm>

namespace cl {

std::int8_t effectiveLaneMidiNote(NoteMap map, int lane, int overrideNote) noexcept CL_NONBLOCKING {
    if (overrideNote >= 1 && overrideNote <= 127)
        return static_cast<std::int8_t>(overrideNote);
    const auto mIdx = static_cast<std::size_t>(std::clamp(static_cast<int>(map), 0, kNoteMapCount - 1));
    const auto lIdx = static_cast<std::size_t>(std::clamp(lane, 0, kDrumLaneCount - 1));
    return kKitNoteMaps[mIdx][lIdx];
}

std::string midiNoteLabel(int midiNote) { return hba::core::midiNoteName(std::clamp(midiNote, 0, 127)); }

} // namespace cl
```

- [x] **Step 5: Rewrite the preset codec**

Replace `core/include/cl/preset.h` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// A Cutline 3 preset: name, category, parameter values by key, expression curves and a groove bank.
// Presets carry "format": 3. Anything older is rejected with kOldFormatError (cl/groove_json.h).
#pragma once

#include "cl/groove.h"
#include "cl/lanes.h"
#include "cl/params.h"

#include <optional>
#include <string>
#include <string_view>

namespace cl {

struct Preset {
    std::string name;
    std::string category;
    Params params;
    LaneCurves curves = defaultLaneCurves();
    GrooveBank bank{};
};

std::optional<Preset> parsePreset(std::string_view json, std::string* error = nullptr);
std::string serialisePreset(const Preset& preset);

std::optional<Curve> parseCurve(std::string_view json);
std::string serialiseCurve(const Curve& curve);

} // namespace cl
```

Replace `core/src/preset.cpp` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/preset.h"

#include "cl/groove_json.h"
#include "groove_json_detail.h"

#include <hba/core/preset_json.h>

#include <string>
#include <string_view>
#include <utility>

namespace cl {

namespace {
using Json = hba::core::Json;

std::optional<float> readValue(const ParamSpec& spec, const Json& value) {
    return hba::core::readParamJson(spec, value, clampToRange);
}

Json writeValue(const ParamSpec& spec, float real) { return hba::core::writeParamJson(spec, real, clampToRange); }

std::string curveKey(int lane) { return hba::core::curveKey(lane); }
int laneOfCurveKey(const std::string& key) { return hba::core::laneOfCurveKey(key, kLaneCount); }
std::optional<Curve> readCurve(const Json& value) { return hba::core::readCurveJson(value); }
Json writeCurve(const Curve& curve) { return hba::core::writeCurveJson(curve); }

void fail(std::string* error, std::string message) {
    if (error != nullptr)
        *error = std::move(message);
}
} // namespace

std::optional<Preset> parsePreset(std::string_view json, std::string* error) {
    const Json root = Json::parse(json, nullptr, false);
    if (!root.is_object()) {
        fail(error, "preset is not a JSON object");
        return std::nullopt;
    }
    const auto format = root.find("format");
    if (format == root.end() || !format->is_number_integer() || format->get<int>() < kGrooveFormat) {
        fail(error, std::string(kOldFormatError));
        return std::nullopt;
    }

    Preset preset;
    for (const auto& [key, value] : root.items()) {
        if (key == "format")
            continue;
        if (key == "name" || key == "category") {
            if (!value.is_string()) {
                fail(error, "\"" + key + "\" must be a string");
                return std::nullopt;
            }
            (key == "name" ? preset.name : preset.category) = value.get<std::string>();
            continue;
        }
        if (key == "bank") {
            const auto bank = detail::readGrooveBank(value, error);
            if (!bank)
                return std::nullopt;
            preset.bank = *bank;
            continue;
        }
        if (const int lane = laneOfCurveKey(key); lane >= 0) {
            const auto curve = readCurve(value);
            if (!curve) {
                fail(error, "bad value for \"" + key + "\"");
                return std::nullopt;
            }
            preset.curves[static_cast<std::size_t>(lane)] = *curve;
            continue;
        }
        const auto id = findParam(key);
        if (!id) {
            fail(error, "unknown parameter \"" + key + "\"");
            return std::nullopt;
        }
        const auto real = readValue(spec(*id), value);
        if (!real) {
            fail(error, "bad value for \"" + key + "\"");
            return std::nullopt;
        }
        preset.params[*id] = *real;
    }
    return preset;
}

std::string serialisePreset(const Preset& preset) {
    Json root = Json::object();
    root["format"] = kGrooveFormat;
    if (!preset.name.empty())
        root["name"] = preset.name;
    if (!preset.category.empty())
        root["category"] = preset.category;
    const Params defaults;
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (preset.params.values[i] != defaults.values[i])
            root[std::string(kParams[i].key)] = writeValue(kParams[i], preset.params.values[i]);
    }
    const LaneCurves defaultCurves = defaultLaneCurves();
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const auto idx = static_cast<std::size_t>(lane);
        if (!(preset.curves[idx] == defaultCurves[idx]))
            root[curveKey(lane)] = writeCurve(preset.curves[idx]);
    }
    root["bank"] = detail::writeGrooveBank(preset.bank);
    return root.dump(2) + "\n";
}

std::optional<Curve> parseCurve(std::string_view json) {
    const Json root = Json::parse(json, nullptr, false);
    return readCurve(root);
}

std::string serialiseCurve(const Curve& curve) { return writeCurve(curve).dump(); }

} // namespace cl
```

- [x] **Step 6: Run the core tests**

```bash
cmake --build --preset engine --target cl-core-tests && build/engine/tests/cl-core-tests
```

Expected: `All tests passed`. `cl_core` builds with no warnings.

- [x] **Step 7: Check formatting and commit**

```bash
xcrun clang-format --style=file:../../.clang-format --dry-run --Werror \
  core/include/cl/params.h core/include/cl/pattern.h core/src/pattern.cpp core/include/cl/preset.h core/src/preset.cpp \
  tests/core/CoreTests.cpp tests/core/PresetTests.cpp
```

Expected: no output for the rewritten files. `params.h` and `pattern.h` were edited in part: if the command reports lines you did not touch, leave those lines alone and fix only yours.

```bash
git add core tests/core tests/CMakeLists.txt
git commit -m "feat(cutline)!: Cutline 3 parameter contract and format-3 presets"
```

---
### Task 3: Engine drum playback, pad programs, truth and determinism

The engine now plays drums from `resolveBar` and `forEachHit` and nothing else. MIDI Slicer mode is silent when this task ends; Task 4 ports it. `engine/src/phrase.cpp`, `smf.cpp` and `styles.cpp` stay on disk but leave the build until Task 4 rewrites or deletes them.

**Files:**
- Create: `engine/include/cl/held_notes.h`, `engine/include/cl/groove_state.h`
- Create: `engine/src/engine_impl.h`, `engine/src/pad_programs.h`, `engine/src/pad_programs.cpp`
- Rewrite: `engine/include/cl/engine.h`, `engine/src/engine.cpp`
- Modify: `engine/src/resolve.cpp` (the `GrooveState` constructors), `engine/src/audition.h` (only if it names a removed type)
- Modify: `engine/CMakeLists.txt`, `tests/CMakeLists.txt`
- Rewrite: `tests/engine/EngineTests.cpp`
- Create: `tests/engine/PadTests.cpp`
- Modify: `docs/specs/2026-11-26-groove-model-design.md` (one sentence about `fillNow`)

**Interfaces:**
- Consumes: `resolveBar`, `buildDensityMap`, `DensityMap`, `ResolveInput`, `ResolvedBar` (`cl/resolve.h`); `forEachHit`, `Hit`, `HitTiming`, `fillScheduled`, `kFillEveryBars`, `maxEarlyBeats`, `maxLateBeats` (`cl/hits.h`); `sanitise`, `patternBars`, `fillWindowStart` (`cl/groove.h`); `effectiveLaneMidiNote(NoteMap, int lane, int overrideNote)` and the Task 2 parameter contract.
- Produces, in `cl/held_notes.h`: `kMaxHeldInputNotes`, `struct HeldNotes` (moved verbatim from the 2.0 `phrase.h`, plus `bool sameAs(const HeldNotes&) const noexcept`).
- Produces, in `cl/groove_state.h`: `struct GrooveState { GrooveBank bank; std::array<DensityMap, kSceneCount> maps; GrooveState() noexcept; explicit GrooveState(const GrooveBank&) noexcept; }`. The constructors sanitise the bank and rank every scene. They are not `CL_NONBLOCKING`: build a `GrooveState` on the message thread.
- Produces, in `cl/engine.h`: `struct PlayPosition { std::int64_t absBar; int barInPattern; int stepInBar; bool fillLive; int repeatStartStep; double repeatBeats; }`; `void Engine::setGroove(const GrooveState&) noexcept CL_NONBLOCKING`; `PlayPosition Engine::position() const noexcept CL_NONBLOCKING`. Removed: `setPatternBank`, `currentPlan`, `currentPhraseIndex`, `playheadPhraseBeats`. Every other accessor keeps its name and meaning.
- Produces, in `engine/src/pad_programs.h` (namespace `cl::pads`): `repeatWindowBeats(float x)`, `repeatGate(float y)`, `rollStepBeats(float x)`, `rollVelocity(float y)`, `rateMultiplier(float x)`, `rateKeepsLane(float y, int lane)`, `dropIsSilence(float x)`.

How drum playback works, so the code below reads easily:

1. Pattern time is `ppq * rate`. Bar `absBar` covers pattern beats `[absBar * 4, absBar * 4 + 4)`.
2. Each block asks for the bars whose hits could fall inside the block: one bar, or two near a bar line, because a hit can start up to `maxEarlyBeats` before its step or `maxLateBeats` after it.
3. For each of those bars it calls `resolveBar`, then `forEachHit`, and keeps the hits whose start lies inside the block.
4. The kept hits are sorted by time in a fixed buffer and then triggered. Sorting matters because two lanes can share a MIDI note (the Strike Body map does), and note-off tracking needs note-ons in time order.
5. `fillNow` is latched once per bar, at the first block that reaches the earliest moment a fill-window hit could start. Before that moment no fill-window hit can sound, so resolving the bar with or without the latch gives the same notes.

- [x] **Step 1: Write the failing engine tests**

Replace `tests/engine/EngineTests.cpp` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// The engine against resolveBar: truth, determinism, fill schedule, rate and position.
#include "cl/engine.h"
#include "cl/groove_state.h"
#include "cl/hits.h"
#include "cl/resolve.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace cl;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 64;

struct NoteOn {
    int note = 0;
    std::int64_t sample = 0;
    int velocity = 0;
    bool operator==(const NoteOn&) const = default;
};

struct Run {
    std::vector<NoteOn> noteOns;
    std::vector<std::uint8_t> bytes; // every event: offset, size, data
    int noteOffs = 0;
    double endBeat = 0.0;
};

int blocksFor(double hostBars, double bpm) {
    return static_cast<int>(std::ceil(hostBars * 4.0 / (bpm / 60.0 / kSr) / static_cast<double>(kBlock)));
}

// Plays `blocks` blocks from ppq 0. `paramsAt(ppq)` supplies the parameters for the block starting at ppq.
template <typename ParamsAt> Run play(Engine& eng, double bpm, int blocks, ParamsAt&& paramsAt) {
    Run out;
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    const double bps = bpm / 60.0 / kSr;
    double ppq = 0.0;
    for (int blk = 0; blk < blocks; ++blk) {
        const Params p = paramsAt(ppq);
        eng.process(p, {bpm, ppq, true}, midi, l.data(), r.data(), kBlock);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            const std::int64_t at = static_cast<std::int64_t>(blk) * kBlock + ev.sampleOffset;
            if (ev.isNoteOn())
                out.noteOns.push_back({ev.noteNumber(), at, static_cast<int>(ev.data[2])});
            else if (ev.isNoteOff())
                ++out.noteOffs;
            out.bytes.push_back(static_cast<std::uint8_t>(ev.sampleOffset));
            out.bytes.push_back(ev.size);
            out.bytes.insert(out.bytes.end(), ev.data, ev.data + 3);
        }
        ppq += bps * static_cast<double>(kBlock);
    }
    out.endBeat = ppq;
    return out;
}

// A seeded generator for test patterns only. The engine itself has no randomness.
struct TestRng {
    std::uint32_t state;
    int range(int lo, int hi) {
        state = state * 1664525u + 1013904223u;
        return lo + static_cast<int>((state >> 8) % static_cast<std::uint32_t>(hi - lo + 1));
    }
    bool chance(int percent) { return range(0, 99) < percent; }
};

void randomStep(TestRng& rng, Step& s, int onPercent) {
    if (!rng.chance(onPercent))
        return;
    s.velocity = static_cast<std::uint8_t>(rng.range(1, 127));
    s.layer = static_cast<Layer>(rng.range(0, 2));
    s.ratchet = static_cast<std::uint8_t>(rng.chance(15) ? rng.range(2, 4) : 1);
    s.nudge = static_cast<std::int8_t>(rng.chance(15) ? rng.range(-24, 24) : 0);
    s.flam = rng.chance(10);
}

Pattern randomPattern(TestRng& rng) {
    Pattern p{};
    p.bars = static_cast<std::uint8_t>(1 << rng.range(0, 2));
    p.fill.beats = static_cast<std::uint8_t>(1 << rng.range(0, 2));
    for (auto& lane : p.lanes) {
        for (int step = 0; step < patternSteps(p); ++step)
            randomStep(rng, lane.steps[static_cast<std::size_t>(step)], 22);
        lane.length = static_cast<std::uint8_t>(rng.chance(20) ? rng.range(3, patternSteps(p)) : 0);
        lane.offsetMs = static_cast<std::int8_t>(rng.chance(30) ? rng.range(-20, 20) : 0);
        lane.muted = rng.chance(8);
    }
    for (auto& row : p.fill.steps) {
        if (!rng.chance(50))
            continue; // this lane draws nothing in the fill and keeps playing the main pattern
        for (auto& s : row)
            randomStep(rng, s, 30);
    }
    return p;
}

struct Expected {
    int lane = 0;
    std::int64_t sample = 0;
    int velocity = 0;
    int bar = 0;
    int step = 0;
    bool grace = false;
};

// What resolveBar and forEachHit say must sound between ppq 0 and `endBeat`, with no pads and a steady fillNow.
std::vector<Expected> expectedNoteOns(const Pattern& pattern, const Params& p, double bpm, double endBeat) {
    std::vector<Expected> out;
    const double rate = kSeqRateMultipliers[static_cast<std::size_t>(p.choice(ParamId::seqRate))];
    const double bps = bpm / 60.0 / kSr;
    const HitTiming timing{static_cast<double>(p[ParamId::swing]), static_cast<double>(p[ParamId::gateLen]),
                           bpm / 60000.0 * rate};
    const int every = kFillEveryBars[static_cast<std::size_t>(p.choice(ParamId::fillEvery))];
    const int bars = patternBars(pattern);
    const auto lastBar = static_cast<std::int64_t>(std::floor(endBeat * rate / 4.0)) + 1;
    for (std::int64_t absBar = -1; absBar <= lastBar; ++absBar) {
        ResolvedBar bar;
        const int inPattern = static_cast<int>(((absBar % bars) + bars) % bars);
        const bool fill = fillScheduled(absBar, every) || p.on(ParamId::fillNow);
        resolveBar(pattern, inPattern, {p[ParamId::density], p[ParamId::dynamics], fill}, bar);
        forEachHit(pattern, bar, static_cast<double>(absBar) * 4.0, timing, [&](const Hit& h) {
            const double host = h.beat / rate;
            if (host < -1.0e-9 || host >= endBeat - 1.0e-9)
                return;
            out.push_back({h.lane, static_cast<std::int64_t>(std::floor(host / bps + 1.0e-7)), h.velocity,
                           static_cast<int>(absBar), h.step, h.grace});
        });
    }
    return out;
}

// Every expected hit is matched by exactly one emitted note-on on the same lane, at the same sample
// (give or take one, for block-edge rounding) and with the same velocity; and nothing else is emitted.
void requireSameNoteOns(const std::vector<Expected>& expected, const std::vector<NoteOn>& actual, int firstNote) {
    REQUIRE(actual.size() == expected.size());
    std::vector<bool> used(actual.size(), false);
    for (const auto& e : expected) {
        bool found = false;
        for (std::size_t i = 0; i < actual.size() && !found; ++i) {
            if (used[i] || actual[i].note - firstNote != e.lane || actual[i].velocity != e.velocity)
                continue;
            if (std::llabs(actual[i].sample - e.sample) > 1)
                continue;
            used[i] = true;
            found = true;
        }
        INFO("lane " << e.lane << " bar " << e.bar << " step " << e.step << (e.grace ? " (grace)" : "") << " sample "
                     << e.sample << " velocity " << e.velocity);
        REQUIRE(found);
    }
}

// Chromatic kit map: lane n plays note 36 + n, so a note-on names its lane.
Params truthParams() {
    Params p;
    p[ParamId::noteMap] = static_cast<float>(NoteMap::chromatic);
    p[ParamId::sceneQuantize] = static_cast<float>(Quantize::instant);
    return p;
}

// Kick on every 16th in the main pattern. The fill (last 2 beats) draws one kick and two snares.
// The hat draws nothing in the fill, so it keeps playing.
Pattern fillPattern() {
    Pattern p{};
    for (int step = 0; step < kStepsPerBar; ++step) {
        p.lanes[0].steps[static_cast<std::size_t>(step)].velocity = 96;
        p.lanes[0].steps[static_cast<std::size_t>(step)].layer = Layer::core;
        if (step % 2 == 0) {
            p.lanes[4].steps[static_cast<std::size_t>(step)].velocity = 96;
            p.lanes[4].steps[static_cast<std::size_t>(step)].layer = Layer::core;
        }
    }
    p.fill.beats = 2;
    p.fill.steps[0][8].velocity = 96;
    p.fill.steps[1][12].velocity = 96;
    p.fill.steps[1][14].velocity = 96;
    return p;
}

int countIn(const Run& run, int note, int bar, double bpm) {
    const double bps = bpm / 60.0 / kSr;
    int n = 0;
    for (const auto& on : run.noteOns) {
        // One sample late on purpose: a hit on a bar line is rounded down to the sample before it.
        const int at = static_cast<int>(std::floor((static_cast<double>(on.sample) + 1.0) * bps / 4.0));
        n += (on.note == note && at == bar) ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("Truth: every note-on the engine emits is a cell resolveBar reports as sounding", "[engine][truth]") {
    for (std::uint32_t seed = 1; seed <= 24; ++seed) {
        TestRng rng{seed * 2654435761u};
        const int scene = rng.range(0, kSceneCount - 1);
        GrooveBank bank;
        bank.scenes[static_cast<std::size_t>(scene)] = randomPattern(rng);
        const GrooveState groove(bank);
        const Pattern& pattern = groove.bank.scenes[static_cast<std::size_t>(scene)];

        Params p = truthParams();
        p[ParamId::scene] = static_cast<float>(scene);
        p[ParamId::density] = static_cast<float>(rng.range(0, 20)) / 20.0f;
        p[ParamId::dynamics] = static_cast<float>(rng.range(0, 20)) / 20.0f;
        p[ParamId::swing] = static_cast<float>(rng.range(0, 20)) / 20.0f;
        p[ParamId::gateLen] = static_cast<float>(rng.range(0, 20)) / 20.0f;
        p[ParamId::fillEvery] = static_cast<float>(rng.range(0, 4));
        p[ParamId::fillNow] = rng.chance(20) ? 1.0f : 0.0f;
        p[ParamId::seqRate] = static_cast<float>(rng.range(0, 2));
        const double bpm = std::vector<double>{87.0, 120.0, 140.0, 172.0}[static_cast<std::size_t>(rng.range(0, 3))];

        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.setGroove(groove);
        const Run run = play(eng, bpm, blocksFor(16.0, bpm), [&](double) { return p; });

        INFO("seed " << seed << " scene " << scene << " bars " << patternBars(pattern) << " bpm " << bpm);
        const auto expected = expectedNoteOns(pattern, p, bpm, run.endBeat);
        REQUIRE(expected.size() > 0);
        requireSameNoteOns(expected, run.noteOns, 36);
    }
}

TEST_CASE("Truth holds for every factory-style density of the default beat", "[engine][truth]") {
    const GrooveState groove;
    for (const float density : {0.0f, 0.5f, 1.0f}) {
        Params p = truthParams();
        p[ParamId::density] = density;
        Engine eng;
        eng.prepare(kSr, kBlock);
        const Run run = play(eng, 172.0, blocksFor(4.0, 172.0), [&](double) { return p; });
        requireSameNoteOns(expectedNoteOns(groove.bank.scenes[0], p, 172.0, run.endBeat), run.noteOns, 36);
    }
}

TEST_CASE("Determinism: the same inputs give byte-identical MIDI, also after reset", "[engine][truth]") {
    TestRng rng{20261126u};
    GrooveBank bank;
    bank.scenes[0] = randomPattern(rng);
    bank.scenes[1] = randomPattern(rng);
    const GrooveState groove(bank);

    // Scene changes, a fill schedule and a Repeat pad hold, all at fixed times.
    const auto paramsAt = [](double ppq) {
        Params p;
        p[ParamId::swing] = 0.4f;
        p[ParamId::fillEvery] = 1.0f;
        p[ParamId::sceneQuantize] = static_cast<float>(Quantize::sixteenth);
        p[ParamId::scene] = ppq >= 16.0 ? 1.0f : 0.0f;
        p[ParamId::padTouch] = (ppq >= 9.0 && ppq < 13.0) ? 1.0f : 0.0f;
        p[ParamId::padX] = 0.6f;
        return p;
    };

    Engine a;
    Engine b;
    a.prepare(kSr, kBlock);
    b.prepare(kSr, kBlock);
    a.setGroove(groove);
    b.setGroove(groove);
    const int blocks = blocksFor(12.0, 140.0);
    const Run first = play(a, 140.0, blocks, paramsAt);
    const Run other = play(b, 140.0, blocks, paramsAt);
    REQUIRE(first.bytes.size() > 100);
    CHECK(first.bytes == other.bytes);

    a.reset();
    const Run again = play(a, 140.0, blocks, paramsAt);
    CHECK(first.bytes == again.bytes);
}

TEST_CASE("A fill fires only in bars where (bar + 1) is a multiple of Fill, and replaces only its window",
          "[engine][fill]") {
    GrooveBank bank;
    bank.scenes[0] = fillPattern();
    Params p;
    p[ParamId::fillEvery] = 2.0f; // every 4 bars
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.setGroove(GrooveState(bank));
    const double bpm = 132.0;
    const Run run = play(eng, bpm, blocksFor(8.0, bpm), [&](double) { return p; });
    for (int bar = 0; bar < 8; ++bar) {
        INFO("bar " << bar);
        const bool fill = bar == 3 || bar == 7;
        CHECK(countIn(run, 36, bar, bpm) == (fill ? 9 : 16)); // steps 0..7 of the main pattern, then one fill kick
        CHECK(countIn(run, 38, bar, bpm) == (fill ? 2 : 0));
        CHECK(countIn(run, 42, bar, bpm) == 8); // the hat draws nothing in the fill and plays through it
    }
}

TEST_CASE("fillNow forces the fill, and is latched once per bar", "[engine][fill]") {
    GrooveBank bank;
    bank.scenes[0] = fillPattern();
    const GrooveState groove(bank);
    const double bpm = 132.0;
    const auto playWith = [&](double from, double to) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.setGroove(groove);
        return play(eng, bpm, blocksFor(3.0, bpm), [&](double ppq) {
            Params p;
            p[ParamId::fillNow] = (ppq >= from && ppq < to) ? 1.0f : 0.0f;
            return p;
        });
    };

    // On for the whole run: every bar plays its fill.
    const Run always = playWith(0.0, 1000.0);
    for (int bar = 0; bar < 3; ++bar)
        CHECK(countIn(always, 38, bar, bpm) == 2);

    // On from beat 1: in time for bar 0's fill window, which starts at beat 2.
    const Run early = playWith(1.0, 1000.0);
    CHECK(countIn(early, 38, 0, bpm) == 2);

    // On from beat 3: bar 0's window has already started, so it waits for bar 1.
    const Run late = playWith(3.0, 1000.0);
    CHECK(countIn(late, 38, 0, bpm) == 0);
    CHECK(countIn(late, 36, 0, bpm) == 16);
    CHECK(countIn(late, 38, 1, bpm) == 2);

    // On until beat 3: bar 0 was latched on and plays its whole fill; bar 1 has none.
    const Run released = playWith(0.0, 3.0);
    CHECK(countIn(released, 38, 0, bpm) == 2);
    CHECK(countIn(released, 36, 0, bpm) == 9);
    CHECK(countIn(released, 38, 1, bpm) == 0);
}

TEST_CASE("Rate plays the pattern at half, normal and double speed", "[engine]") {
    const GrooveState groove;
    std::size_t counts[3]{};
    for (int rate = 0; rate < 3; ++rate) {
        Params p = truthParams();
        p[ParamId::seqRate] = static_cast<float>(rate);
        Engine eng;
        eng.prepare(kSr, kBlock);
        const Run run = play(eng, 140.0, blocksFor(4.0, 140.0), [&](double) { return p; });
        requireSameNoteOns(expectedNoteOns(groove.bank.scenes[0], p, 140.0, run.endBeat), run.noteOns, 36);
        counts[rate] = run.noteOns.size();
    }
    CHECK(counts[1] > counts[0] + 10);
    CHECK(counts[2] > counts[1] + 20);
}

TEST_CASE("Stopping the transport leaves no note sounding", "[engine]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    const Run run = play(eng, 172.0, blocksFor(2.0, 172.0), [](double) { return Params{}; });
    REQUIRE(run.noteOns.size() > 10);
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    eng.process(Params{}, {172.0, run.endBeat, false}, midi, l.data(), r.data(), kBlock);
    CHECK(eng.activeNotesLowMask() == 0);
    CHECK(eng.activeNotesHighMask() == 0);
    for (const float v : l)
        REQUIRE(std::isfinite(v));
}

TEST_CASE("The engine reports where it is in the pattern", "[engine]") {
    GrooveBank bank;
    bank.scenes[0] = fillPattern();
    setPatternBars(bank.scenes[0], 2);
    Params p;
    p[ParamId::fillEvery] = 1.0f; // every 2 bars: bars 1, 3, 5...
    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.setGroove(GrooveState(bank));
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;

    eng.process(p, {120.0, 5.3, true}, midi, l.data(), r.data(), kBlock); // bar 1, beat 1.3
    PlayPosition pos = eng.position();
    CHECK(pos.absBar == 1);
    CHECK(pos.barInPattern == 1);
    CHECK(pos.stepInBar == 5);
    CHECK(pos.fillLive);
    CHECK(pos.repeatStartStep == -1);

    eng.process(p, {120.0, 8.0, true}, midi, l.data(), r.data(), kBlock); // bar 2 is bar 0 of the pattern
    pos = eng.position();
    CHECK(pos.absBar == 2);
    CHECK(pos.barInPattern == 0);
    CHECK(pos.stepInBar == 0);
    CHECK_FALSE(pos.fillLive);
}
```

Create `tests/engine/PadTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// The four interim pad programs: Repeat, Roll, Rate, Drop.
#include "cl/engine.h"
#include "cl/groove_state.h"
#include "pad_programs.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

using namespace cl;
using Catch::Approx;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 64;
constexpr double kBpm = 132.0;

struct NoteOn {
    int note = 0;
    std::int64_t sample = 0;
    int velocity = 0;
    bool operator==(const NoteOn&) const = default;
};

struct Run {
    std::vector<NoteOn> noteOns;
    std::map<int, int> open; // note -> note-ons minus note-offs
    int controlChanges = 0;
    double endBeat = 0.0;
};

template <typename ParamsAt> Run play(Engine& eng, double hostBars, ParamsAt&& paramsAt) {
    Run out;
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    const double bps = kBpm / 60.0 / kSr;
    const int blocks = static_cast<int>(std::ceil(hostBars * 4.0 / bps / static_cast<double>(kBlock)));
    double ppq = 0.0;
    const auto take = [&](int blk) {
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            if (ev.isNoteOn()) {
                out.noteOns.push_back(
                    {ev.noteNumber(), static_cast<std::int64_t>(blk) * kBlock + ev.sampleOffset, static_cast<int>(ev.data[2])});
                // A retrigger sends the old note's note-off first, so a note is never open twice.
                ++out.open[ev.noteNumber()];
            } else if (ev.isNoteOff()) {
                --out.open[ev.noteNumber()];
            } else if ((ev.data[0] & 0xF0u) == 0xB0u) {
                ++out.controlChanges;
            }
        }
    };
    for (int blk = 0; blk < blocks; ++blk) {
        eng.process(paramsAt(ppq), {kBpm, ppq, true}, midi, l.data(), r.data(), kBlock);
        take(blk);
        ppq += bps * static_cast<double>(kBlock);
    }
    eng.process(paramsAt(ppq), {kBpm, ppq, false}, midi, l.data(), r.data(), kBlock); // stop: flushes what is held
    take(blocks);
    out.endBeat = ppq;
    return out;
}

Params padParams(PadProgram program, float x, float y) {
    Params p;
    p[ParamId::sceneQuantize] = static_cast<float>(Quantize::instant);
    p[ParamId::padProgram] = static_cast<float>(program);
    p[ParamId::padTouch] = 1.0f;
    p[ParamId::padX] = x;
    p[ParamId::padY] = y;
    return p;
}

int beatOf(const NoteOn& on) {
    // One sample late on purpose: a hit on a beat is rounded down to the sample before it.
    return static_cast<int>(std::floor((static_cast<double>(on.sample) + 1.0) * (kBpm / 60.0 / kSr)));
}

int countInBeat(const Run& run, int note, int beat) {
    int n = 0;
    for (const auto& on : run.noteOns)
        n += (on.note == note && beatOf(on) == beat) ? 1 : 0;
    return n;
}

// Default beat with a drawn two-beat snare fill.
GrooveState beatWithFill() {
    GrooveBank bank;
    bank.scenes[0].fill.beats = 2;
    bank.scenes[0].fill.steps[1][10].velocity = 96;
    bank.scenes[0].fill.steps[1][13].velocity = 96;
    return GrooveState(bank);
}

} // namespace

TEST_CASE("Pad X and Y map to the documented steps", "[pads]") {
    CHECK(pads::repeatWindowBeats(0.0f) == Approx(1.0));
    CHECK(pads::repeatWindowBeats(0.3f) == Approx(0.5));
    CHECK(pads::repeatWindowBeats(0.6f) == Approx(0.25));
    CHECK(pads::repeatWindowBeats(1.0f) == Approx(0.125));
    CHECK(pads::repeatGate(0.0f) == Approx(0.20));
    CHECK(pads::repeatGate(1.0f) == Approx(1.05));
    CHECK(pads::rollStepBeats(0.0f) == Approx(0.5));
    CHECK(pads::rollStepBeats(0.3f) == Approx(0.25));
    CHECK(pads::rollStepBeats(0.6f) == Approx(1.0 / 6.0));
    CHECK(pads::rollStepBeats(1.0f) == Approx(0.125));
    CHECK(pads::rollVelocity(0.0f) == 1);
    CHECK(pads::rollVelocity(1.0f) == 127);
    CHECK(pads::rateMultiplier(0.1f) == Approx(0.5));
    CHECK(pads::rateMultiplier(0.5f) == Approx(1.0));
    CHECK(pads::rateMultiplier(0.9f) == Approx(2.0));
    CHECK(pads::rateKeepsLane(0.1f, 0));
    CHECK(pads::rateKeepsLane(0.1f, 1));
    CHECK_FALSE(pads::rateKeepsLane(0.1f, 4));
    CHECK(pads::rateKeepsLane(0.5f, 4));
    CHECK_FALSE(pads::rateKeepsLane(0.9f, 0));
    CHECK(pads::rateKeepsLane(0.9f, 4));
    CHECK(pads::dropIsSilence(0.2f));
    CHECK_FALSE(pads::dropIsSilence(0.8f));
}

TEST_CASE("Every pad program releases all its notes", "[engine][pads]") {
    for (int program = 0; program < kPerfPadCount; ++program) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.setGroove(beatWithFill());
        const Run run = play(eng, 6.0, [&](double ppq) {
            const float sweep = static_cast<float>(std::fmod(ppq, 8.0) / 8.0);
            Params p = padParams(static_cast<PadProgram>(program), 0.05f + 0.9f * sweep, 0.95f - 0.9f * sweep);
            p[ParamId::padTouch] = (ppq >= 2.0 && ppq < 18.0) ? 1.0f : 0.0f;
            return p;
        });
        INFO("program " << program);
        CHECK(run.noteOns.size() > 8);
        for (const auto& [note, open] : run.open)
            CHECK(open == 0);
        CHECK(eng.activeNotesLowMask() == 0);
        CHECK(eng.activeNotesHighMask() == 0);
        CHECK(run.controlChanges == 0); // XY -> CC is Off by default
    }
}

TEST_CASE("A C3 note does not engage a pad with default parameters", "[engine][pads]") {
    Engine plain;
    plain.prepare(kSr, kBlock);
    const Run reference = play(plain, 2.0, [](double) { return Params{}; });

    Engine eng;
    eng.prepare(kSr, kBlock);
    eng.inputNoteOn(60, 100, 0.0);
    const Run run = play(eng, 2.0, [](double) { return Params{}; });
    CHECK_FALSE(eng.padEngaged());
    CHECK(run.noteOns == reference.noteOns);

    // With Pad MIDI switched on, C#3 engages Roll.
    Engine armed;
    armed.prepare(kSr, kBlock);
    armed.inputNoteOn(61, 100, 0.0);
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    Params p;
    p[ParamId::padMidi] = 1.0f;
    p[ParamId::sceneQuantize] = static_cast<float>(Quantize::instant);
    armed.process(p, {kBpm, 0.0, true}, midi, l.data(), r.data(), kBlock);
    CHECK(armed.padEngaged());
    CHECK(armed.activePadProgram() == static_cast<int>(PadProgram::roll));
}

TEST_CASE("Repeat loops the hits inside its window", "[engine][pads]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    // Engaged on beat 0 with a one-beat window: kick and hat on the beat, hat on the 8th, every beat.
    const Run run = play(eng, 2.0, [](double) { return padParams(PadProgram::repeat, 0.1f, 0.5f); });
    for (int beat = 1; beat <= 6; ++beat) {
        INFO("beat " << beat);
        CHECK(countInBeat(run, 36, beat) == 1);
        CHECK(countInBeat(run, 42, beat) == 2);
        CHECK(countInBeat(run, 38, beat) == 0);
    }
    // play() ends by stopping the transport, which clears the Repeat telemetry. Run one more block.
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    eng.process(padParams(PadProgram::repeat, 0.1f, 0.5f), {kBpm, run.endBeat, true}, midi, l.data(), r.data(), kBlock);
    CHECK(eng.position().repeatStartStep == 0);
    CHECK(eng.position().repeatBeats == Approx(1.0));
}

TEST_CASE("Roll plays the snare lane's note on a grid, at the velocity Y sets", "[engine][pads]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    const Run run = play(eng, 2.0, [](double) { return padParams(PadProgram::roll, 0.3f, 1.0f); });
    REQUIRE(run.noteOns.size() > 16);
    for (const auto& on : run.noteOns) {
        CHECK(on.note == 38);
        CHECK(on.velocity == 127);
    }
    for (int beat = 1; beat <= 6; ++beat)
        CHECK(countInBeat(run, 38, beat) == 4); // 1/16
}

TEST_CASE("Rate changes the speed and Y filters the kit", "[engine][pads]") {
    Engine pad;
    pad.prepare(kSr, kBlock);
    const Run doubled = play(pad, 2.0, [](double) { return padParams(PadProgram::rate, 0.9f, 0.5f); });

    Engine param;
    param.prepare(kSr, kBlock);
    const Run reference = play(param, 2.0, [](double) {
        Params p;
        p[ParamId::seqRate] = static_cast<float>(SeqRate::doubleTime);
        return p;
    });
    CHECK(doubled.noteOns == reference.noteOns);

    Engine low;
    low.prepare(kSr, kBlock);
    const Run kickSnare = play(low, 2.0, [](double) { return padParams(PadProgram::rate, 0.5f, 0.1f); });
    REQUIRE(kickSnare.noteOns.size() > 4);
    for (const auto& on : kickSnare.noteOns)
        CHECK((on.note == 36 || on.note == 38));

    Engine high;
    high.prepare(kSr, kBlock);
    const Run noKick = play(high, 2.0, [](double) { return padParams(PadProgram::rate, 0.5f, 0.9f); });
    REQUIRE(noKick.noteOns.size() > 4);
    for (const auto& on : noKick.noteOns)
        CHECK(on.note != 36);
}

TEST_CASE("Drop is silence on the left and the fill on the right", "[engine][pads]") {
    Engine left;
    left.prepare(kSr, kBlock);
    left.setGroove(beatWithFill());
    const Run silent = play(left, 2.0, [](double) { return padParams(PadProgram::drop, 0.2f, 0.5f); });
    CHECK(silent.noteOns.empty());

    Engine right;
    right.prepare(kSr, kBlock);
    right.setGroove(beatWithFill());
    const Run dropped = play(right, 2.0, [](double) { return padParams(PadProgram::drop, 0.8f, 0.5f); });

    Engine param;
    param.prepare(kSr, kBlock);
    param.setGroove(beatWithFill());
    const Run reference = play(param, 2.0, [](double) {
        Params p;
        p[ParamId::fillNow] = 1.0f;
        return p;
    });
    REQUIRE(reference.noteOns.size() > 10);
    CHECK(dropped.noteOns == reference.noteOns);
}

TEST_CASE("Releasing a pad returns to the pattern in phase", "[engine][pads]") {
    Engine plain;
    plain.prepare(kSr, kBlock);
    const Run reference = play(plain, 4.0, [](double) { return Params{}; });

    for (int program = 0; program < kPerfPadCount; ++program) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        const Run run = play(eng, 4.0, [&](double ppq) {
            Params p = padParams(static_cast<PadProgram>(program), 0.9f, 0.2f);
            p[ParamId::padTouch] = (ppq >= 4.0 && ppq < 6.0) ? 1.0f : 0.0f;
            return p;
        });
        const auto from = static_cast<std::int64_t>(7.0 / (kBpm / 60.0 / kSr));
        std::vector<NoteOn> a, b;
        for (const auto& on : run.noteOns)
            if (on.sample >= from)
                a.push_back(on);
        for (const auto& on : reference.noteOns)
            if (on.sample >= from)
                b.push_back(on);
        INFO("program " << program);
        REQUIRE(b.size() > 10);
        CHECK(a == b);
    }
}
```

In `tests/CMakeLists.txt`, replace the `cl-engine-tests` source list with:

```cmake
add_executable(cl-engine-tests
    engine/EngineTests.cpp
    engine/PadTests.cpp
    engine/ResolveTests.cpp
    engine/HitsTests.cpp
    engine/PatternLintTests.cpp
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/GainTests.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/dsp/tests/CurveTests.cpp")
```

In `engine/CMakeLists.txt`, replace the `add_library(cl_engine STATIC ...)` call with:

```cmake
add_library(cl_engine STATIC
    include/cl/engine.h
    include/cl/groove_state.h
    include/cl/held_notes.h
    include/cl/hits.h
    include/cl/midi_event.h
    include/cl/resolve.h
    src/audition.h
    src/engine.cpp
    src/engine_impl.h
    src/pad_programs.cpp
    src/pad_programs.h
    src/resolve.cpp)
```

- [x] **Step 2: Run them to verify they fail**

```bash
cmake --build --preset engine --target cl-engine-tests
```

Expected: CMake reports that `src/engine_impl.h` or `include/cl/groove_state.h` cannot be found, or the compiler reports `'cl/groove_state.h' file not found`.

- [x] **Step 3: Add `held_notes.h` and `groove_state.h`**

Create `engine/include/cl/held_notes.h`. Move `kMaxHeldInputNotes` and the whole `struct HeldNotes` out of `engine/include/cl/phrase.h` unchanged (the 2.0 `phrase.h` is not compiled again until Task 4 rewrites it), and add `sameAs`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// The notes held on the MIDI input, lowest first. Fixed capacity, safe on the audio thread.
#pragma once

#include "cl/nonblocking.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cl {

inline constexpr int kMaxHeldInputNotes = 16;

struct HeldNotes {
    // ... notes, velocities, count, lastPressed, clear(), push(), remove(): moved verbatim from phrase.h ...

    // True when both hold the same notes at the same velocities.
    bool sameAs(const HeldNotes& other) const noexcept CL_NONBLOCKING {
        if (count != other.count || lastPressed != other.lastPressed)
            return false;
        for (int i = 0; i < count; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            if (notes[idx] != other.notes[idx] || velocities[idx] != other.velocities[idx])
                return false;
        }
        return true;
    }
};

} // namespace cl
```

Create `engine/include/cl/groove_state.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// What the engine plays from: a sanitised bank and, for each scene, the Density ranking of its steps.
// Building one ranks every scene, so build it on the message thread and hand it to the audio thread whole.
#pragma once

#include "cl/groove.h"
#include "cl/resolve.h"

#include <array>

namespace cl {

struct GrooveState {
    GrooveBank bank{};
    std::array<DensityMap, kSceneCount> maps{};

    GrooveState() noexcept;                             // the default bank
    explicit GrooveState(const GrooveBank& source) noexcept;
};

} // namespace cl
```

At the end of `engine/src/resolve.cpp`, inside `namespace cl`, add (and add `#include "cl/groove_state.h"` at the top):

```cpp
GrooveState::GrooveState() noexcept : GrooveState(GrooveBank{}) {}

GrooveState::GrooveState(const GrooveBank& source) noexcept : bank(source) {
    sanitise(bank);
    for (std::size_t scene = 0; scene < bank.scenes.size(); ++scene)
        buildDensityMap(bank.scenes[scene], maps[scene]);
}
```

- [x] **Step 4: Add the pad mappings**

Create `engine/src/pad_programs.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// X/Y mappings of the four interim pad programs. The PLAY spec replaces these.
#pragma once

#include "cl/nonblocking.h"

#include <algorithm>
#include <cmath>

namespace cl::pads {

// Repeat X: a window of 1, 1/2, 1/4 or 1/8 beat.
constexpr double repeatWindowBeats(float x) noexcept CL_NONBLOCKING {
    return x < 0.25f ? 1.0 : (x < 0.5f ? 0.5 : (x < 0.75f ? 0.25 : 0.125));
}

// Repeat Y: gate length, from 20% to 105% of each note's own length.
constexpr double repeatGate(float y) noexcept CL_NONBLOCKING {
    return 0.20 + 0.85 * static_cast<double>(std::clamp(y, 0.0f, 1.0f));
}

// Roll X: 1/8, 1/16, 1/16 triplet or 1/32, in beats.
constexpr double rollStepBeats(float x) noexcept CL_NONBLOCKING {
    return x < 0.25f ? 0.5 : (x < 0.5f ? 0.25 : (x < 0.75f ? 1.0 / 6.0 : 0.125));
}

// Roll Y: velocity 1..127.
inline int rollVelocity(float y) noexcept CL_NONBLOCKING {
    return std::clamp(static_cast<int>(std::lround(1.0f + 126.0f * std::clamp(y, 0.0f, 1.0f))), 1, 127);
}

// Rate X: 0.5x, 1x or 2x, on top of the Rate parameter.
constexpr double rateMultiplier(float x) noexcept CL_NONBLOCKING {
    return x < 1.0f / 3.0f ? 0.5 : (x > 2.0f / 3.0f ? 2.0 : 1.0);
}

// Rate Y: below 30% kick and snare only; above 70% no kick.
constexpr bool rateKeepsLane(float y, int lane) noexcept CL_NONBLOCKING {
    if (y < 0.30f)
        return lane <= 1;
    if (y > 0.70f)
        return lane != 0;
    return true;
}

// Drop X: the left half is silence, the right half plays the fill (the same as fillNow).
constexpr bool dropIsSilence(float x) noexcept CL_NONBLOCKING { return x < 0.5f; }

} // namespace cl::pads
```

- [x] **Step 5: Rewrite the engine header**

Replace `engine/include/cl/engine.h` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline 3 real-time MIDI engine. Drum notes come only from resolveBar and forEachHit.
// Pure C++20, no JUCE, [[clang::nonblocking]] verified.
#pragma once

#include "cl/groove_state.h"
#include "cl/held_notes.h"
#include "cl/lanes.h"
#include "cl/midi_event.h"
#include "cl/nonblocking.h"
#include "cl/params.h"
#include "cl/pattern.h"

#include <array>
#include <cstdint>
#include <memory>

namespace cl {

struct Transport {
    double bpm = 172.0;
    double ppq = 0.0;
    bool playing = false;
};

enum ActivityBit : std::uint8_t {
    kActNote = 1u << 0u,
    kActCc = 1u << 1u,
    kActPad = 1u << 2u,
    kActSlice = 1u << 3u,
    kActClip = 1u << 4u,
};

// Where the engine is in the active scene's pattern.
struct PlayPosition {
    std::int64_t absBar = 0;  // floor(ppq * rate / 4)
    int barInPattern = 0;     // absBar mod the pattern's bars
    int stepInBar = 0;        // 0..15
    bool fillLive = false;    // this bar plays its fill
    int repeatStartStep = -1; // step in the pattern where the Repeat window starts; -1 when Repeat is not engaged
    double repeatBeats = 0.0; // length of the Repeat window in beats
};

class Engine {
  public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept CL_NONBLOCKING;

    void setLaneCurves(const LaneCurves& curves) noexcept CL_NONBLOCKING;
    void setGroove(const GrooveState& groove) noexcept CL_NONBLOCKING;

    // Live MIDI input: C3-D#3 engage the pad programs when Pad MIDI is on (drum mode); in MIDI Slicer
    // mode the notes are the chord to slice, or the clip to capture.
    void inputNoteOn(int midiNote, int velocity, double ppq = -1.0) noexcept CL_NONBLOCKING;
    void inputNoteOff(int midiNote, double ppq = -1.0) noexcept CL_NONBLOCKING;
    void inputSustain(bool down) noexcept CL_NONBLOCKING;
    void inputAllNotesOff() noexcept CL_NONBLOCKING;

    // Writes sample-accurate MIDI into `outMidi` and, when `left` and `right` are not null, the audition kit.
    void process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left, float* right,
                 int numSamples) noexcept CL_NONBLOCKING;

    // Telemetry for the 60 Hz UI.
    PlayPosition position() const noexcept CL_NONBLOCKING;
    HeldNotes activeHeldNotes() const noexcept CL_NONBLOCKING;
    int activeScene() const noexcept CL_NONBLOCKING;
    int pendingScene() const noexcept CL_NONBLOCKING;
    int activePadProgram() const noexcept CL_NONBLOCKING;
    int pendingPadProgram() const noexcept CL_NONBLOCKING;
    bool padEngaged() const noexcept CL_NONBLOCKING;
    std::uint8_t midiPadMask() const noexcept CL_NONBLOCKING;
    int capturedClipCount() const noexcept CL_NONBLOCKING;
    double lanePpq() const noexcept CL_NONBLOCKING;
    std::array<float, kLaneCount> lanePhases() const noexcept CL_NONBLOCKING;
    std::array<float, kLaneCount> laneValues() const noexcept CL_NONBLOCKING;
    std::array<float, kDrumLaneCount> laneHitFlashes() const noexcept CL_NONBLOCKING;
    std::uint64_t activeNotesLowMask() const noexcept CL_NONBLOCKING;
    std::uint64_t activeNotesHighMask() const noexcept CL_NONBLOCKING;
    int lastBlockEventCount() const noexcept CL_NONBLOCKING;
    std::uint8_t lastActivityMask() const noexcept CL_NONBLOCKING;
    bool isPlaying() const noexcept CL_NONBLOCKING;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cl
```

- [x] **Step 6: Declare `Engine::Impl`**

Create `engine/src/engine_impl.h`. `engine.cpp` and `pad_programs.cpp` both include it.

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Engine::Impl, shared by engine.cpp (transport, scheduling, emitting) and pad_programs.cpp (the pads).
#pragma once

#include "cl/engine.h"
#include "cl/hits.h"

#include "audition.h"

#include <array>
#include <cstdint>
#include <limits>

namespace cl {

inline constexpr int kMaxActiveNotes = 32;
inline constexpr int kMaxCapturedEvents = 128;
inline constexpr int kMaxBlockHits = 192;
inline constexpr double kBeatEps = 1.0e-9;

struct Engine::Impl {
    struct ActiveNote {
        bool active = false;
        std::uint8_t channel = 1;
        std::int8_t note = 0;
        double absEndBeat = 0.0;
    };

    struct CapturedEvent {
        double barBeat = 0.0;
        double durBeats = 0.25;
        std::int8_t note = 60;
        std::uint8_t vel = 100;
        std::int64_t barIndex = -1;
        bool active = false;
    };

    // A note this block will start, in host beats.
    struct BlockHit {
        double beat = 0.0;
        double durBeats = 0.1;
        std::int8_t note = 0;
        std::int8_t lane = 0; // drum lane, or the chord voice in MIDI Slicer mode
        std::uint8_t velocity = 0;
        bool drum = true;
    };

    // fillNow as sampled for one bar.
    struct FillLatch {
        std::int64_t bar = std::numeric_limits<std::int64_t>::min();
        bool on = false;
    };

    // One block's transport and controls, shared by playback and the pad programs.
    struct Block {
        const Params* params = nullptr;
        double rawStart = 0.0;    // host beats at sample 0 of the block
        double windowStart = 0.0; // host beats: notes starting in [windowStart, windowEnd) belong to this block
        double windowEnd = 0.0;
        double beatsPerSample = 0.0;
        int numSamples = 0;
        double rate = 1.0;       // pattern beats per host beat: Rate, times the Rate pad while it is engaged
        double beatsPerMs = 0.0; // pattern beats per millisecond
        NoteMap noteMap = NoteMap::liveRack;
        PadProgram program = PadProgram::repeat;
        bool isDrum = true;
        float padX = 0.5f;
        float padY = 0.5f;
    };

    double sampleRate = 48000.0;
    LaneCurves curves = defaultLaneCurves();
    GrooveState groove{};
    std::uint32_t grooveRevision = 0;

    // Live MIDI input
    HeldNotes physicalHeld{};
    HeldNotes held{};
    HeldNotes latched{};
    HeldNotes lastEffectiveHeld{};
    bool sustainDown = false;
    bool wasSlicerLatch = false;
    std::uint8_t midiPadHeld = 0;
    int lastMidiPadProg = -1;

    // Rolling one-bar clip capture for SlicerStyle::clipSlice
    std::array<CapturedEvent, kMaxCapturedEvents> captured{};
    int capturedCount = 0;
    int capturedWrite = 0;
    std::int64_t lastCaptureBar = -1;

    std::array<ActiveNote, kMaxActiveNotes> active{};
    std::array<BlockHit, kMaxBlockHits> blockHits{};
    int blockHitCount = 0;

    // Quantized scene and pad state
    int currentScene = 0;
    int pendingSceneIdx = -1;
    double sceneSwitchBeat = 0.0;
    int activeProg = 0;
    int pendingProg = -1;
    bool engaged = false;
    bool pendingEngage = false;
    double padSwitchBeat = 0.0;

    // Repeat: the window that is looping, in pattern beats
    double repeatAnchor = 0.0;
    std::int64_t repeatBar = 0;
    bool repeatFill = false;

    std::array<FillLatch, 2> fillLatch{};

    // Transport tracking
    bool wasPlaying = false;
    double expectedNextPpq = 0.0;
    double lastTransportPpq = 0.0;
    EngineMode lastEngineMode = EngineMode::drum;
    SlicerStyle lastSlicerStyle = SlicerStyle::stabChop;

    // CC deduplication
    int lastCc74 = -1;
    int lastCc1 = -1;
    int lastCc11 = -1;
    int lastCc71 = -1;
    int lastPressure = -1;
    int lastBend = 8192;

    // Telemetry
    PlayPosition pos{};
    double lastLanePpq = 0.0;
    std::array<float, kLaneCount> phases{};
    std::array<float, kLaneCount> values{};
    std::array<float, kDrumLaneCount> laneFlashes{};
    int lastEventCount = 0;
    std::uint8_t activityMask = 0;
    bool playingState = false;

    AuditionSynth audition{};

    // engine.cpp
    void prepare(double sr) noexcept CL_NONBLOCKING;
    void clearDedupe() noexcept CL_NONBLOCKING;
    void reset() noexcept CL_NONBLOCKING;
    const HeldNotes& effectiveHeld(const Params& params) const noexcept CL_NONBLOCKING;
    const Pattern& pattern() const noexcept CL_NONBLOCKING;
    const DensityMap& densityMap() const noexcept CL_NONBLOCKING;
    void flushAllActive(MidiEventBuffer& outMidi, int sampleOffset) noexcept CL_NONBLOCKING;
    void triggerNote(std::int8_t noteNum, int velocity, int laneHint, double absEndBeat, int offset,
                     MidiEventBuffer& outMidi, bool isDrum) noexcept CL_NONBLOCKING;
    void recordClipNoteOn(int midiNote, int vel, double ppq) noexcept CL_NONBLOCKING;
    void recordClipNoteOff(int midiNote, double ppq) noexcept CL_NONBLOCKING;
    void emitContinuousAt(const Params& params, double tickBeat, int offset, MidiEventBuffer& outMidi, bool padOn,
                          float padX, float padY) noexcept CL_NONBLOCKING;
    int evalVelBias(const Params& params, double beat, bool padOn, float padY) const noexcept CL_NONBLOCKING;
    bool fillActiveFor(std::int64_t absBar, bool fillNow, int everyBars, double scaledEnd,
                       double beatsPerMs) noexcept CL_NONBLOCKING;
    void pushDrumHit(const Block& b, int lane, double hostBeat, double hostDur, int velocity) noexcept CL_NONBLOCKING;
    void playPattern(const Block& b, bool fillNow) noexcept CL_NONBLOCKING;
    void emitBlockHits(const Block& b, MidiEventBuffer& outMidi) noexcept CL_NONBLOCKING;
    void process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left, float* right,
                 int numSamples) noexcept CL_NONBLOCKING;
    void renderAuditionAudio(const Params& params, float* left, float* right, int numSamples) noexcept CL_NONBLOCKING;

    // pad_programs.cpp
    void beginRepeat(double hostBeat, double rate, bool fillNow, int everyBars) noexcept CL_NONBLOCKING;
    void playRepeat(const Block& b) noexcept CL_NONBLOCKING;
    void playRoll(const Block& b) noexcept CL_NONBLOCKING;
};

// Mathematical modulo: the result is in 0..n-1 for negative `a` too.
constexpr int floorMod(std::int64_t a, int n) noexcept CL_NONBLOCKING {
    return static_cast<int>(((a % n) + n) % n);
}

} // namespace cl
```

- [x] **Step 7: Rewrite `engine.cpp`**

Start from the 2.0 file and change it as follows.

**Includes.** Replace `#include "cl/engine.h"`, `"audition.h"` and `"styles.h"` with `#include "engine_impl.h"` and `#include "pad_programs.h"`. Keep `<hba/dsp/denormals.h>`; drop `<hba/dsp/lane_math.h>` (nothing hashes any more).

**Anonymous namespace.** Keep `toMidi7Bit`, `toMidi7BitUni`, `toPitchBend14`, `sampleOffsetOf` and `quantizeStepBeats` unchanged. Delete `kMaxActiveNotes`, `kMaxCapturedEvents` (they moved to `engine_impl.h`), `rollRateBeatsForX`, `stutterWindowBeatsForX` and `bounceStepBeats`.

**`struct Engine::Impl { ... }`.** Delete the in-class definition. Each member function below becomes an out-of-class definition, `ReturnType Engine::Impl::name(...) noexcept CL_NONBLOCKING { ... }`.

**Kept with their 2.0 bodies:** `prepare`, `clearDedupe`, `effectiveHeld`, `flushAllActive`, `triggerNote`, `recordClipNoteOn`, `recordClipNoteOff`, `evalVelBias`, `renderAuditionAudio`.

**Deleted:** `buildEffectiveCompilationInputs`, `ensurePlan`.

**`reset`** becomes:

```cpp
void Engine::Impl::reset() noexcept CL_NONBLOCKING {
    for (auto& slot : active)
        slot.active = false;
    blockHitCount = 0;
    physicalHeld.clear();
    held.clear();
    latched.clear();
    lastEffectiveHeld.clear();
    sustainDown = false;
    wasSlicerLatch = false;
    midiPadHeld = 0;
    lastMidiPadProg = -1;
    for (auto& c : captured)
        c.active = false;
    capturedCount = 0;
    capturedWrite = 0;
    lastCaptureBar = -1;
    currentScene = 0;
    pendingSceneIdx = -1;
    sceneSwitchBeat = 0.0;
    activeProg = 0;
    pendingProg = -1;
    engaged = false;
    pendingEngage = false;
    padSwitchBeat = 0.0;
    repeatAnchor = 0.0;
    repeatBar = 0;
    repeatFill = false;
    fillLatch = {};
    wasPlaying = false;
    expectedNextPpq = 0.0;
    lastTransportPpq = 0.0;
    lastEngineMode = EngineMode::drum;
    lastSlicerStyle = SlicerStyle::stabChop;
    pos = {};
    lastLanePpq = 0.0;
    phases.fill(0.0f);
    values.fill(0.0f);
    laneFlashes.fill(0.0f);
    lastEventCount = 0;
    activityMask = 0;
    playingState = false;
    clearDedupe();
    audition.reset();
}
```

`reset` keeps `groove` and `curves`: they are the session, not the playback state.

**`emitContinuousAt`** loses its `PadProgram prog` parameter and the Drop special case. Drop no longer sends CC74 and CC1 on its own. The three lines that decide what the pad drives become:

```cpp
    const auto xyCc = static_cast<PadCcMode>(std::clamp(params.choice(ParamId::padCcMode), 0, 3));
    const bool xyDrivesCc74 = padOn && (xyCc == PadCcMode::cc74AndCc1 || xyCc == PadCcMode::cc74AndVel);
    const bool xyDrivesCc1 = padOn && (xyCc == PadCcMode::cc74AndCc1 || xyCc == PadCcMode::cc1AndAt);
    const bool xyDrivesAt = padOn && (xyCc == PadCcMode::cc1AndAt);
```

and the CC1 value is `xyDrivesCc1 ? static_cast<double>(padX) : (...)`. The rest of the function is unchanged.

**New helpers:**

```cpp
const Pattern& Engine::Impl::pattern() const noexcept CL_NONBLOCKING {
    return groove.bank.scenes[static_cast<std::size_t>(std::clamp(currentScene, 0, kSceneCount - 1))];
}

const DensityMap& Engine::Impl::densityMap() const noexcept CL_NONBLOCKING {
    return groove.maps[static_cast<std::size_t>(std::clamp(currentScene, 0, kSceneCount - 1))];
}

// True when bar `absBar` plays its fill. The schedule is a pure function of the bar. fillNow is sampled
// once per bar, at the first block that reaches the earliest moment a fill-window hit could start
// (a full early nudge, lane offset and flam before the window). Before that moment no fill-window hit
// can sound, so the bar resolves to the same notes whichever way the latch will fall.
bool Engine::Impl::fillActiveFor(std::int64_t absBar, bool fillNow, int everyBars, double scaledEnd,
                                 double beatsPerMs) noexcept CL_NONBLOCKING {
    const bool scheduled = fillScheduled(absBar, everyBars);
    FillLatch& latch = fillLatch[static_cast<std::size_t>(absBar & 1)];
    if (latch.bar != absBar) {
        const double samplePoint = static_cast<double>(absBar) * 4.0 +
                                   static_cast<double>(fillWindowStart(pattern().fill)) * 0.25 -
                                   maxEarlyBeats(beatsPerMs);
        if (scaledEnd < samplePoint)
            return scheduled;
        latch = {absBar, fillNow};
    }
    return scheduled || latch.on;
}

void Engine::Impl::pushDrumHit(const Block& b, int lane, double hostBeat, double hostDur,
                               int velocity) noexcept CL_NONBLOCKING {
    if (blockHitCount >= kMaxBlockHits)
        return;
    BlockHit& hit = blockHits[static_cast<std::size_t>(blockHitCount++)];
    hit.beat = hostBeat;
    hit.durBeats = hostDur;
    hit.note = effectiveLaneMidiNote(b.noteMap, lane, pattern().lanes[static_cast<std::size_t>(lane)].midiNote);
    hit.lane = static_cast<std::int8_t>(lane);
    hit.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
    hit.drum = true;
}

// The pattern itself: resolve every bar that can reach into this block, and keep the hits that start in it.
void Engine::Impl::playPattern(const Block& b, bool fillNow) noexcept CL_NONBLOCKING {
    const Params& params = *b.params;
    const Pattern& pat = pattern();
    const int bars = patternBars(pat);
    const int everyBars = kFillEveryBars[static_cast<std::size_t>(std::clamp(params.choice(ParamId::fillEvery), 0, 4))];
    const HitTiming timing{static_cast<double>(params[ParamId::swing]), static_cast<double>(params[ParamId::gateLen]),
                           b.beatsPerMs};
    const double scaledStart = b.windowStart * b.rate;
    const double scaledEnd = b.windowEnd * b.rate;
    const auto firstBar = static_cast<std::int64_t>(std::floor((scaledStart - maxLateBeats(b.beatsPerMs)) / 4.0));
    const auto lastBar = static_cast<std::int64_t>(std::floor((scaledEnd + maxEarlyBeats(b.beatsPerMs)) / 4.0));
    const bool filterKit = engaged && b.program == PadProgram::rate;

    for (std::int64_t bar = firstBar; bar <= lastBar && bar < firstBar + 4; ++bar) {
        const bool fill = fillActiveFor(bar, fillNow, everyBars, scaledEnd, b.beatsPerMs);
        ResolvedBar resolved;
        resolveBar(pat, densityMap(), floorMod(bar, bars), {params[ParamId::density], params[ParamId::dynamics], fill},
                   resolved);
        forEachHit(pat, resolved, static_cast<double>(bar) * 4.0, timing, [&](const Hit& h) noexcept CL_NONBLOCKING {
            if (h.beat < scaledStart - kBeatEps || h.beat >= scaledEnd - kBeatEps)
                return;
            if (filterKit && !pads::rateKeepsLane(b.padY, h.lane))
                return;
            pushDrumHit(b, h.lane, h.beat / b.rate, h.durBeats / b.rate, h.velocity);
        });
    }
}

// Sorts this block's hits by time (stable, so equal times keep lane order) and starts them.
void Engine::Impl::emitBlockHits(const Block& b, MidiEventBuffer& outMidi) noexcept CL_NONBLOCKING {
    for (int i = 1; i < blockHitCount; ++i) {
        const BlockHit key = blockHits[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && blockHits[static_cast<std::size_t>(j)].beat > key.beat) {
            blockHits[static_cast<std::size_t>(j + 1)] = blockHits[static_cast<std::size_t>(j)];
            --j;
        }
        blockHits[static_cast<std::size_t>(j + 1)] = key;
    }
    for (int i = 0; i < blockHitCount; ++i) {
        const BlockHit& hit = blockHits[static_cast<std::size_t>(i)];
        const int offset = sampleOffsetOf(hit.beat, b.rawStart, b.beatsPerSample, b.numSamples);
        const int velocity = static_cast<int>(hit.velocity) + evalVelBias(*b.params, hit.beat, engaged, b.padY);
        triggerNote(hit.note, velocity, hit.lane, hit.beat + std::max(0.03, hit.durBeats), offset, outMidi, hit.drum);
    }
    blockHitCount = 0;
}
```

**`process`** becomes:

```cpp
void Engine::Impl::process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left,
                           float* right, int numSamples) noexcept CL_NONBLOCKING {
    hba::dsp::ScopedFlushDenormals ftz;
    outMidi.clear();
    if (numSamples <= 0)
        return;

    const float flashDecay = static_cast<float>(std::exp(-static_cast<double>(numSamples) / (0.115 * sampleRate)));
    for (auto& f : laneFlashes)
        f = (f * flashDecay < 0.005f) ? 0.0f : (f * flashDecay);

    const double bpm = std::clamp(transport.bpm, 20.0, 400.0);
    const double beatsPerSample = bpm / 60.0 / sampleRate;
    const auto rateIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::seqRate), 0, 2));
    const double baseRate = kSeqRateMultipliers[rateIdx];
    const auto mode = static_cast<EngineMode>(std::clamp(params.choice(ParamId::engineMode), 0, 1));
    const auto slicerStyle = static_cast<SlicerStyle>(std::clamp(params.choice(ParamId::slicerStyle), 0, 4));
    const auto quant = static_cast<Quantize>(std::clamp(params.choice(ParamId::sceneQuantize), 0, 3));
    const bool isDrum = (mode == EngineMode::drum);
    const int everyBars = kFillEveryBars[static_cast<std::size_t>(std::clamp(params.choice(ParamId::fillEvery), 0, 4))];

    const bool latchNow = params.on(ParamId::slicerLatch);
    if (wasSlicerLatch && !latchNow)
        latched = held;
    wasSlicerLatch = latchNow;
    lastEffectiveHeld = effectiveHeld(params);

    // What the pad surface is asked to do: touch, hold, or C3-D#3 when Pad MIDI is on.
    const bool midiPadActive = isDrum && params.on(ParamId::padMidi) && (midiPadHeld != 0u);
    const int reqProg = (midiPadActive && lastMidiPadProg >= 0)
                            ? std::clamp(lastMidiPadProg, 0, kPerfPadCount - 1)
                            : std::clamp(params.choice(ParamId::padProgram), 0, kPerfPadCount - 1);
    const bool reqEngaged = params.on(ParamId::padTouch) || params.on(ParamId::padHold) || midiPadActive;
    const float padX = std::clamp(params[ParamId::padX], 0.0f, 1.0f);
    const float padY = std::clamp(params[ParamId::padY], 0.0f, 1.0f);
    const int reqScene = std::clamp(params.choice(ParamId::scene), 0, kSceneCount - 1);

    const double rawPpq = transport.ppq; // may be negative during host pre-roll
    lastTransportPpq = rawPpq;
    lastLanePpq = rawPpq;
    playingState = transport.playing;
    activityMask = 0;

    // A pad that starts sounding at `hostBeat`.
    const auto beginPad = [&](int program, double hostBeat, int offset) noexcept CL_NONBLOCKING {
        if (static_cast<PadProgram>(program) == PadProgram::repeat)
            beginRepeat(hostBeat, baseRate, params.on(ParamId::fillNow), everyBars);
        else if (static_cast<PadProgram>(program) == PadProgram::drop && pads::dropIsSilence(padX))
            flushAllActive(outMidi, offset);
    };

    // Quantized scene and pad switching.
    const double qStep = quantizeStepBeats(quant);
    if (!transport.playing || qStep <= 0.0) {
        currentScene = reqScene;
        pendingSceneIdx = -1;
        const bool newlyEngaged = reqEngaged && (!engaged || reqProg != activeProg);
        activeProg = reqProg;
        engaged = reqEngaged;
        pendingProg = -1;
        pendingEngage = false;
        if (newlyEngaged)
            beginPad(activeProg, rawPpq, 0);
    } else {
        if (reqScene != currentScene && reqScene != pendingSceneIdx) {
            pendingSceneIdx = reqScene;
            sceneSwitchBeat = std::ceil((rawPpq - 1e-6) / qStep) * qStep;
        } else if (reqScene == currentScene) {
            pendingSceneIdx = -1;
        }
        if (!reqEngaged) {
            engaged = false; // release at once, so letting go on beat 1 is tight
            pendingEngage = false;
            pendingProg = -1;
            activeProg = reqProg;
        } else if (!engaged || reqProg != activeProg) {
            if (!pendingEngage || pendingProg != reqProg) {
                pendingEngage = true;
                pendingProg = reqProg;
                padSwitchBeat = std::ceil((rawPpq - 1e-6) / qStep) * qStep;
            }
        } else {
            pendingEngage = false;
            pendingProg = -1;
        }
    }

    for (int lane = 0; lane < kLaneCount; ++lane) {
        const auto idx = static_cast<std::size_t>(lane);
        phases[idx] = static_cast<float>(
            lanePhase(rawPpq, laneLengthQuarters(params, lane), params[laneParam(lane, LaneField::phase)]));
        values[idx] = static_cast<float>(laneSend(params, curves, lane, rawPpq));
    }

    if (mode != lastEngineMode || slicerStyle != lastSlicerStyle) {
        flushAllActive(outMidi, 0);
        clearDedupe();
        lastEngineMode = mode;
        lastSlicerStyle = slicerStyle;
    }
    if (!isDrum && slicerStyle != SlicerStyle::clipSlice && lastEffectiveHeld.count == 0)
        flushAllActive(outMidi, 0);

    // Position telemetry, from the unshifted transport.
    const auto updatePosition = [&](double rate, bool fillLive) noexcept CL_NONBLOCKING {
        const double scaled = rawPpq * rate;
        const auto absBar = static_cast<std::int64_t>(std::floor(scaled / 4.0));
        pos.absBar = absBar;
        pos.barInPattern = floorMod(absBar, patternBars(pattern()));
        pos.stepInBar = std::clamp(static_cast<int>(std::floor((scaled - static_cast<double>(absBar) * 4.0) * 4.0)), 0,
                                   kStepsPerBar - 1);
        pos.fillLive = fillLive;
        pos.repeatStartStep = -1;
        pos.repeatBeats = 0.0;
    };

    if (!transport.playing) {
        if (wasPlaying)
            flushAllActive(outMidi, 0);
        wasPlaying = false;
        updatePosition(baseRate, false);
        lastEventCount = outMidi.count;
        renderAuditionAudio(params, left, right, numSamples);
        return;
    }

    const double shiftBeats = static_cast<double>(params[ParamId::shiftMs]) * 1.0e-3 * (bpm / 60.0);
    const double rawStart = transport.ppq - shiftBeats;
    const double rawEnd = rawStart + static_cast<double>(numSamples) * beatsPerSample;

    if (!wasPlaying || transport.ppq < expectedNextPpq - 1.0e-4 || std::abs(transport.ppq - expectedNextPpq) > 0.125) {
        flushAllActive(outMidi, 0);
        clearDedupe();
        fillLatch = {};
    }
    wasPlaying = true;
    expectedNextPpq = transport.ppq + static_cast<double>(numSamples) * beatsPerSample;

    // Hit times are not clamped: the window follows the transport below beat 0 too.
    const double windowStart = rawStart;
    const double windowEnd = rawEnd;

    // Commit quantized scene and pad switches on their boundary.
    if (pendingSceneIdx >= 0 && windowEnd >= sceneSwitchBeat - kBeatEps) {
        currentScene = pendingSceneIdx;
        pendingSceneIdx = -1;
    }
    if (pendingEngage && windowEnd >= padSwitchBeat - kBeatEps) {
        activeProg = pendingProg >= 0 ? pendingProg : reqProg;
        engaged = true;
        pendingEngage = false;
        pendingProg = -1;
        const double at = std::max(windowStart, padSwitchBeat);
        beginPad(activeProg, padSwitchBeat, sampleOffsetOf(at, rawStart, beatsPerSample, numSamples));
    }

    Block b;
    b.params = &params;
    b.rawStart = rawStart;
    b.windowStart = windowStart;
    b.windowEnd = windowEnd;
    b.beatsPerSample = beatsPerSample;
    b.numSamples = numSamples;
    b.program = static_cast<PadProgram>(std::clamp(activeProg, 0, kPerfPadCount - 1));
    b.rate = baseRate * ((engaged && b.program == PadProgram::rate) ? pads::rateMultiplier(padX) : 1.0);
    b.beatsPerMs = bpm / 60000.0 * b.rate;
    b.noteMap = static_cast<NoteMap>(std::clamp(params.choice(ParamId::noteMap), 0, kNoteMapCount - 1));
    b.isDrum = isDrum;
    b.padX = padX;
    b.padY = padY;

    // Drop's right half is fillNow.
    const bool dropFill = engaged && b.program == PadProgram::drop && !pads::dropIsSilence(padX);
    const bool fillNow = params.on(ParamId::fillNow) || dropFill;
    {
        const auto absBar = static_cast<std::int64_t>(std::floor(rawPpq * b.rate / 4.0));
        updatePosition(b.rate, isDrum && fillActiveFor(absBar, fillNow, everyBars, windowEnd * b.rate, b.beatsPerMs));
    }

    // 1. Note-offs that fall inside this block.
    for (auto& slot : active) {
        if (slot.active && slot.absEndBeat < windowEnd - kBeatEps) {
            const int offset = sampleOffsetOf(slot.absEndBeat, rawStart, beatsPerSample, numSamples);
            outMidi.pushCritical(MidiEvent::noteOff(offset, slot.channel, slot.note));
            audition.noteOff(slot.note);
            slot.active = false;
        }
    }

    // 2. This block's notes: a pad program, or the pattern.
    blockHitCount = 0;
    if (isDrum) {
        if (engaged && b.program == PadProgram::roll)
            playRoll(b);
        else if (engaged && b.program == PadProgram::repeat)
            playRepeat(b);
        else if (!(engaged && b.program == PadProgram::drop && pads::dropIsSilence(padX)))
            playPattern(b, fillNow);
    }
    // MIDI Slicer playback is added in Task 4.
    emitBlockHits(b, outMidi);

    // 3. Continuous CC, expression lanes and XY -> CC.
    const auto thinIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::ccThinMs), 0, 3));
    const double tickBeats = kCcThinMsValues[thinIdx] * 1.0e-3 * (bpm / 60.0);
    std::int64_t tickIdx = static_cast<std::int64_t>(std::ceil((windowStart - kBeatEps) / tickBeats));
    for (int iter = 0; iter < 128; ++iter) {
        const double tickBeat = static_cast<double>(tickIdx) * tickBeats;
        if (tickBeat >= windowEnd - kBeatEps)
            break;
        if (tickBeat >= windowStart - kBeatEps) {
            const int offset = sampleOffsetOf(tickBeat, rawStart, beatsPerSample, numSamples);
            emitContinuousAt(params, tickBeat, offset, outMidi, engaged, padX, padY);
        }
        ++tickIdx;
    }

    outMidi.sort();
    lastEventCount = outMidi.count;

    std::uint8_t act = 0;
    for (int i = 0; i < outMidi.count; ++i) {
        const auto& ev = outMidi.events[static_cast<std::size_t>(i)];
        const std::uint8_t hi = ev.data[0] & 0xF0u;
        if (ev.isNoteOn())
            act |= kActNote;
        else if (hi == 0xB0u || hi == 0xD0u || hi == 0xE0u)
            act |= kActCc;
    }
    if (engaged)
        act |= kActPad;
    if (!isDrum && lastEffectiveHeld.count > 0)
        act |= kActSlice;
    if (capturedCount > 0)
        act |= kActClip;
    activityMask = act;

    renderAuditionAudio(params, left, right, numSamples);
}
```

**The `Engine::` forwarding functions** at the bottom of the file keep their bodies, with these changes: `setPatternBank` becomes

```cpp
void Engine::setGroove(const GrooveState& groove) noexcept CL_NONBLOCKING {
    impl_->groove = groove;
    ++impl_->grooveRevision;
}
```

`currentPlan`, `currentPhraseIndex` and `playheadPhraseBeats` are deleted, and

```cpp
PlayPosition Engine::position() const noexcept CL_NONBLOCKING { return impl_->pos; }
```

is added. `inputNoteOn` and `inputNoteOff` are unchanged: they already use `kPerfPadCount`, which is now 4, so C3 to D#3 are the pad notes.

If `engine/src/audition.h` fails to compile because it names a type Task 2 removed, change only that line to the Task 2 equivalent and say so in your report.

- [x] **Step 8: Add the pad programs**

Create `engine/src/pad_programs.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// The four interim pad programs. Each is locked to the grid, and each returns to the pattern in phase on
// release, because the pattern's position always comes from the transport. Rate and Drop need no code of
// their own here: Rate is a multiplier and a lane filter in playPattern, Drop is silence or fillNow.
#include "engine_impl.h"
#include "pad_programs.h"

#include <algorithm>
#include <cmath>

namespace cl {

// Remembers the 16th the pad was engaged on. The window loops from there.
void Engine::Impl::beginRepeat(double hostBeat, double rate, bool fillNow, int everyBars) noexcept CL_NONBLOCKING {
    const double scaled = hostBeat * rate;
    repeatAnchor = std::floor(scaled * 4.0 + 1.0e-6) * 0.25;
    repeatBar = static_cast<std::int64_t>(std::floor(repeatAnchor / 4.0));
    repeatFill = fillScheduled(repeatBar, everyBars) || fillNow;
}

// Repeat: loops the resolved hits that start inside the window. X is the window, Y the gate length.
void Engine::Impl::playRepeat(const Block& b) noexcept CL_NONBLOCKING {
    const Params& params = *b.params;
    const Pattern& pat = pattern();
    const int bars = patternBars(pat);
    const double window = pads::repeatWindowBeats(b.padX);
    const double gate = pads::repeatGate(b.padY);
    const double barStart = static_cast<double>(repeatBar) * 4.0;
    const double start = std::min(repeatAnchor, barStart + 4.0 - window); // keep the window inside its bar
    const double scaledStart = b.windowStart * b.rate;
    const double scaledEnd = b.windowEnd * b.rate;
    const HitTiming timing{static_cast<double>(params[ParamId::swing]), static_cast<double>(params[ParamId::gateLen]),
                           b.beatsPerMs};

    ResolvedBar resolved;
    resolveBar(pat, densityMap(), floorMod(repeatBar, bars),
               {params[ParamId::density], params[ParamId::dynamics], repeatFill}, resolved);

    const auto firstCycle =
        std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor((scaledStart - repeatAnchor) / window)));
    const auto lastCycle = static_cast<std::int64_t>(std::floor((scaledEnd - repeatAnchor) / window));
    for (std::int64_t cycle = firstCycle; cycle <= lastCycle && cycle < firstCycle + 64; ++cycle) {
        const double origin = repeatAnchor + static_cast<double>(cycle) * window;
        forEachHit(pat, resolved, barStart, timing, [&](const Hit& h) noexcept CL_NONBLOCKING {
            const double offset = h.beat - start;
            if (offset < 0.0 || offset >= window)
                return;
            const double at = origin + offset;
            if (at < scaledStart - kBeatEps || at >= scaledEnd - kBeatEps)
                return;
            const double dur = std::min(h.durBeats, window * 0.85) * gate;
            pushDrumHit(b, h.lane, at / b.rate, std::max(0.025, dur / b.rate), h.velocity);
        });
    }

    pos.repeatStartStep = floorMod(repeatBar, bars) * kStepsPerBar + static_cast<int>(std::lround((start - barStart) * 4.0));
    pos.repeatBeats = window;
}

// Roll: the snare lane's note on a grid. X is the rate, Y the velocity.
void Engine::Impl::playRoll(const Block& b) noexcept CL_NONBLOCKING {
    const double step = pads::rollStepBeats(b.padX) / b.rate; // host beats
    const int velocity = pads::rollVelocity(b.padY);
    auto index = static_cast<std::int64_t>(std::ceil((b.windowStart - kBeatEps) / step));
    for (int guard = 0; guard < 64; ++guard, ++index) {
        const double beat = static_cast<double>(index) * step;
        if (beat >= b.windowEnd - kBeatEps)
            break;
        if (b.isDrum)
            pushDrumHit(b, 1, beat, step * 0.8, velocity);
    }
}

} // namespace cl
```

- [x] **Step 9: Build and run the engine tests**

```bash
cmake --build --preset engine --target cl-engine-tests cl-core-tests && build/engine/tests/cl-engine-tests && build/engine/tests/cl-core-tests
```

Expected: both print `All tests passed`. `cl_engine` compiles under `-Werror=function-effects`; a "function with 'nonblocking' attribute must not ..." error means something on the audio path allocates, locks or calls an unannotated function whose body the compiler cannot see.

If a truth case fails, the failure message names the lane, bar, step and sample of the first missing note. Do not loosen the test. The usual causes are a hit dropped at a bar line (check the `firstBar` / `lastBar` range) and a fill flag that differs from `fillScheduled(absBar) || fillNow`.

- [x] **Step 10: Confirm nothing random is left in what this task built**

```bash
grep -nE "laneHash|Rng|uniform\(|rand\(" engine/src/engine.cpp engine/src/engine_impl.h engine/src/pad_programs.cpp engine/src/pad_programs.h engine/src/resolve.cpp
```

Expected: no output.

- [x] **Step 11: Update the spec's `fillNow` sentence**

In `docs/specs/2026-11-26-groove-model-design.md`, in the Fills section, replace

```
`fillNow` is sampled half a beat
before the fill window starts, so that hits nudged early are not missed; switching it on later
affects the next bar.
```

with

```
`fillNow` is sampled once per
bar, at the earliest moment a hit in the fill window could start (a full early nudge, lane offset and
flam before the window), so that hits nudged early are not missed; switching it on later affects the
next bar.
```

- [x] **Step 12: Check formatting and commit**

```bash
xcrun clang-format --style=file:../../.clang-format --dry-run --Werror \
  engine/include/cl/engine.h engine/include/cl/groove_state.h engine/include/cl/held_notes.h \
  engine/src/engine.cpp engine/src/engine_impl.h engine/src/pad_programs.cpp engine/src/pad_programs.h engine/src/resolve.cpp \
  tests/engine/EngineTests.cpp tests/engine/PadTests.cpp
```

Expected: no output.

```bash
git add engine tests docs/specs
git commit -m "feat(cutline)!: play drums from resolveBar and forEachHit, with four interim pad programs"
```

---
### Task 4: MIDI Slicer, Clip Slice and MIDI export; delete the 2.0 generator

When this task ends the whole `engine` preset is green again.

**Files:**
- Rewrite: `engine/include/cl/phrase.h`, `engine/src/phrase.cpp`
- Rewrite: `engine/include/cl/smf.h`, `engine/src/smf.cpp`
- Modify: `engine/include/cl/engine.h`, `engine/src/engine_impl.h`, `engine/src/engine.cpp`, `engine/src/pad_programs.cpp`
- Modify: `tools/cutline_render.cpp`
- Delete: `engine/src/styles.cpp`, `engine/src/styles.h`, `tools/seed_patterns.py`
- Modify: `engine/CMakeLists.txt`, `tests/CMakeLists.txt`
- Create: `tests/engine/SlicerTests.cpp`, `tests/engine/SmfTests.cpp`

**Interfaces:**
- Consumes: `GrooveState`, `HeldNotes`, `Engine::Impl`, `Block`, `BlockHit`, `kBeatEps` (Task 3); `swingBeat`, `forEachHit`, `HitTiming` (`cl/hits.h`); `resolveBar`, `applyDynamics` (`cl/resolve.h`); `hba::core::SmfEvent`, `hba::core::encodeSmfType0` (`<hba/core/smf.h>`).
- Produces, in `cl/phrase.h`: `struct SlicerInputs { ...; bool sameAs(const SlicerInputs&) const; }` and `SlicerInputs slicerInputs(const Params&, int scene, const HeldNotes&, double bpm, std::uint32_t grooveRevision)` (what a compiled phrase depends on, so the engine and the UI model recompile only on change); `struct SlicerNote { double beat; double durBeats; std::int8_t note; std::int8_t voice; std::int16_t step; std::uint8_t velocity; }`; `inline constexpr int kMaxSlicerNotes = 768`; `struct SlicerPlan { double phraseBeats; int noteCount; std::array<SlicerNote, kMaxSlicerNotes> notes; }`; `void compileSlicer(const Params&, const GrooveState&, int scene, const HeldNotes&, double bpm, SlicerPlan& out) noexcept CL_NONBLOCKING`. Removed: `CompiledNote`, `PhrasePlan`, `kMaxPhraseNotes`, `warpBeat`, `phraseSignature`, `compilePhrase`, `appendDrumFill`.
- Produces, in `cl/smf.h`: `inline constexpr std::uint16_t kSmfPpq = 960`; `std::vector<std::uint8_t> exportSmf(const Params&, const LaneCurves&, const HeldNotes&, double bpm, int scene, const GrooveBank& bank)`. Removed: `exportPhraseSmf`.
- Produces, in `cl/engine.h`: `const SlicerPlan& Engine::slicerPlan() const noexcept CL_NONBLOCKING`.

What the slicer keeps and loses, from the spec:

- The gate strip is `bank.slicerSteps`: a step sounds if it is on, at its velocity, with its ratchet, nudge and flam. `Layer` is ignored.
- The phrase is as long as the active scene's pattern. The gate loops inside it every `slicerLength` steps.
- Bass / Stab plays the lowest held note where the kick lane of the resolved bar sounds (Density applies, fills do not), and the other held notes on the gate, except on the first step of each bar.
- Chop, random ratchets, the octave echo and velocity jitter are gone. Break Arp always walks up through the held notes.
- Swing replaces the pocket warp. Flam on a gate step strums that step.
- Slicer note times stay clamped inside the phrase, as in 2.0. Only drum hits are unclamped.
- If a phrase would hold more than 768 notes, the notes of the last steps are dropped, as in 2.0.

- [x] **Step 1: Write the failing slicer tests**

Create `tests/engine/SlicerTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/engine.h"
#include "cl/phrase.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <vector>

using namespace cl;
using Catch::Approx;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 64;

HeldNotes chord() {
    HeldNotes held;
    held.push(48, 100);
    held.push(55, 90);
    held.push(60, 80);
    return held;
}

Params slicerParams(SlicerStyle style) {
    Params p;
    p[ParamId::engineMode] = static_cast<float>(EngineMode::slicer);
    p[ParamId::slicerStyle] = static_cast<float>(style);
    return p;
}

std::unique_ptr<SlicerPlan> compile(const Params& p, const GrooveState& groove, const HeldNotes& held, int scene = 0,
                                    double bpm = 120.0) {
    auto plan = std::make_unique<SlicerPlan>();
    compileSlicer(p, groove, scene, held, bpm, *plan);
    return plan;
}

int notesAt(const SlicerPlan& plan, double beat) {
    int n = 0;
    for (int i = 0; i < plan.noteCount; ++i)
        n += std::abs(plan.notes[static_cast<std::size_t>(i)].beat - beat) < 1.0e-6 ? 1 : 0;
    return n;
}

} // namespace

TEST_CASE("Stab Chop plays every held note on every lit gate step", "[slicer]") {
    const GrooveState groove; // default gate: steps 0, 3, 6, 10, 14
    const auto plan = compile(slicerParams(SlicerStyle::stabChop), groove, chord());
    CHECK(plan->phraseBeats == Approx(4.0));
    REQUIRE(plan->noteCount == 15);
    for (const double beat : {0.0, 0.75, 1.5, 2.5, 3.5})
        CHECK(notesAt(*plan, beat) == 3);
    for (int i = 0; i < plan->noteCount; ++i) {
        const int note = plan->notes[static_cast<std::size_t>(i)].note;
        CHECK((note == 48 || note == 55 || note == 60));
    }
}

TEST_CASE("The slicer phrase is as long as the scene's pattern", "[slicer]") {
    GrooveBank bank;
    setPatternBars(bank.scenes[2], 4);
    const auto plan = compile(slicerParams(SlicerStyle::stabChop), GrooveState(bank), chord(), 2);
    CHECK(plan->phraseBeats == Approx(16.0));
    CHECK(plan->noteCount == 60); // the 16-step gate loops four times
    CHECK(notesAt(*plan, 12.75) == 3);
}

TEST_CASE("A gate step ratchets only when its ratchet says so, and Swing moves odd steps", "[slicer]") {
    GrooveBank bank;
    bank.slicerSteps[3].ratchet = 3;
    Params p = slicerParams(SlicerStyle::stabChop);
    auto plan = compile(p, GrooveState(bank), chord());
    CHECK(plan->noteCount == 21); // four plain steps and one triple step, three notes each
    CHECK(notesAt(*plan, 0.75) == 3);
    CHECK(notesAt(*plan, 0.75 + 0.25 / 3.0) == 3);
    CHECK(notesAt(*plan, 0.0) == 3);

    p[ParamId::swing] = 1.0f;
    plan = compile(p, GrooveState(GrooveBank{}), chord());
    CHECK(notesAt(*plan, 0.875) == 3); // step 3 is odd: delayed by an eighth of a beat
    CHECK(notesAt(*plan, 1.5) == 3);   // step 6 is even: unmoved
}

TEST_CASE("Flam on a gate step strums it, and Strum / Flam strums every step", "[slicer]") {
    GrooveBank bank;
    bank.slicerSteps[6].flam = true;
    Params p = slicerParams(SlicerStyle::stabChop);
    p[ParamId::strumMs] = 20.0f; // 0.04 beats at 120 BPM
    auto plan = compile(p, GrooveState(bank), chord());
    CHECK(notesAt(*plan, 0.0) == 3);
    CHECK(notesAt(*plan, 1.5) == 1);
    CHECK(notesAt(*plan, 1.54) == 1);
    CHECK(notesAt(*plan, 1.58) == 1);

    p[ParamId::slicerStyle] = static_cast<float>(SlicerStyle::strumFlam);
    plan = compile(p, GrooveState(GrooveBank{}), chord());
    CHECK(notesAt(*plan, 0.0) == 1);
    CHECK(notesAt(*plan, 0.04) == 1);
}

TEST_CASE("Break Arp walks up through the held notes, one per lit step", "[slicer]") {
    const GrooveState groove;
    const auto plan = compile(slicerParams(SlicerStyle::breakArp), groove, chord());
    REQUIRE(plan->noteCount == 5);
    const int expected[5]{48, 55, 60, 48, 55};
    for (int i = 0; i < 5; ++i)
        CHECK(plan->notes[static_cast<std::size_t>(i)].note == expected[i]);
}

TEST_CASE("Bass / Stab takes its bass rhythm from the resolved kick lane", "[slicer]") {
    GrooveBank bank; // default beat: kick on steps 0 and 8
    bank.scenes[0].lanes[0].steps[6].velocity = 96;
    bank.scenes[0].lanes[0].steps[6].layer = Layer::detail;
    const GrooveState groove(bank);
    Params p = slicerParams(SlicerStyle::bassStab);

    auto plan = compile(p, groove, chord());
    int bass = 0;
    int upper = 0;
    for (int i = 0; i < plan->noteCount; ++i) {
        const auto& n = plan->notes[static_cast<std::size_t>(i)];
        if (n.note == 48) {
            ++bass;
            CHECK((std::abs(n.beat - 0.0) < 1.0e-6 || std::abs(n.beat - 2.0) < 1.0e-6));
        } else {
            ++upper;
        }
    }
    CHECK(bass == 2);  // the Detail kick is silent at Density 50%
    CHECK(upper == 8); // gate steps 3, 6, 10, 14: two upper notes each; step 0 is left to the bass

    p[ParamId::density] = 1.0f;
    plan = compile(p, groove, chord());
    bass = 0;
    for (int i = 0; i < plan->noteCount; ++i)
        bass += plan->notes[static_cast<std::size_t>(i)].note == 48 ? 1 : 0;
    CHECK(bass == 3); // at Density 100% the Detail kick sounds, so the bass follows it
}

TEST_CASE("Clip Slice and an empty chord compile to nothing, and compiling twice gives the same phrase", "[slicer]") {
    const GrooveState groove;
    CHECK(compile(slicerParams(SlicerStyle::clipSlice), groove, chord())->noteCount == 0);
    CHECK(compile(slicerParams(SlicerStyle::stabChop), groove, HeldNotes{})->noteCount == 0);

    Params p = slicerParams(SlicerStyle::strumFlam);
    p[ParamId::swing] = 0.5f;
    const auto a = compile(p, groove, chord());
    const auto b = compile(p, groove, chord());
    REQUIRE(a->noteCount == b->noteCount);
    for (int i = 0; i < a->noteCount; ++i) {
        const auto& x = a->notes[static_cast<std::size_t>(i)];
        const auto& y = b->notes[static_cast<std::size_t>(i)];
        CHECK(x.beat == y.beat);
        CHECK(x.note == y.note);
        CHECK(x.velocity == y.velocity);
    }
}

TEST_CASE("Slicer inputs compare equal until something the phrase depends on changes", "[slicer]") {
    Params p = slicerParams(SlicerStyle::stabChop);
    const SlicerInputs a = slicerInputs(p, 0, chord(), 120.0, 7);
    CHECK_FALSE(a.sameAs(SlicerInputs{})); // nothing compiled yet
    CHECK(a.sameAs(slicerInputs(p, 0, chord(), 120.0, 7)));
    CHECK_FALSE(a.sameAs(slicerInputs(p, 1, chord(), 120.0, 7)));
    CHECK_FALSE(a.sameAs(slicerInputs(p, 0, chord(), 121.0, 7)));
    CHECK_FALSE(a.sameAs(slicerInputs(p, 0, chord(), 120.0, 8)));
    CHECK_FALSE(a.sameAs(slicerInputs(p, 0, HeldNotes{}, 120.0, 7)));
    p[ParamId::swing] = 0.2f;
    CHECK_FALSE(a.sameAs(slicerInputs(p, 0, chord(), 120.0, 7)));
}

TEST_CASE("The engine plays the slicer phrase: your notes only, and no pitch bend", "[engine][slicer]") {
    const std::set<int> allowed{41, 48, 56, 60, 67};
    for (int style = 0; style < 4; ++style) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        for (const int note : allowed)
            eng.inputNoteOn(note, 100);
        Params p = slicerParams(static_cast<SlicerStyle>(style));
        p[ParamId::slicerLatch] = 1.0f;

        std::vector<float> l(kBlock), r(kBlock);
        MidiEventBuffer midi;
        const double bps = 132.0 / 60.0 / kSr;
        double ppq = 0.0;
        int noteOns = 0;
        int pitchBends = 0;
        for (int blk = 0; blk < 3000; ++blk) {
            eng.process(p, {132.0, ppq, true}, midi, l.data(), r.data(), kBlock);
            for (int i = 0; i < midi.count; ++i) {
                const auto& ev = midi.events[static_cast<std::size_t>(i)];
                if (ev.isNoteOn()) {
                    ++noteOns;
                    CHECK(allowed.count(ev.noteNumber()) == 1);
                }
                pitchBends += (ev.data[0] & 0xF0u) == 0xE0u ? 1 : 0;
            }
            for (const float v : l)
                REQUIRE(std::isfinite(v));
            ppq += bps * static_cast<double>(kBlock);
        }
        INFO("style " << style);
        CHECK(noteOns > 8);
        CHECK(pitchBends == 0);
        CHECK(eng.slicerPlan().noteCount > 0);

        eng.process(p, {132.0, ppq, false}, midi, l.data(), r.data(), kBlock);
        CHECK(eng.activeNotesLowMask() == 0);
        CHECK(eng.activeNotesHighMask() == 0);
    }
}

TEST_CASE("The engine emits exactly the compiled slicer phrase, phrase after phrase", "[engine][slicer]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    const HeldNotes held = chord();
    for (int i = 0; i < held.count; ++i)
        eng.inputNoteOn(held.notes[static_cast<std::size_t>(i)], held.velocities[static_cast<std::size_t>(i)]);
    const Params p = slicerParams(SlicerStyle::stabChop);

    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    const double bpm = 120.0;
    const double bps = bpm / 60.0 / kSr;
    double ppq = 0.0;
    int noteOns = 0;
    const int blocks = static_cast<int>(std::ceil(7.9 / bps / static_cast<double>(kBlock))); // just under two phrases
    for (int blk = 0; blk < blocks; ++blk) {
        eng.process(p, {bpm, ppq, true}, midi, l.data(), r.data(), kBlock);
        for (int i = 0; i < midi.count; ++i)
            noteOns += midi.events[static_cast<std::size_t>(i)].isNoteOn() ? 1 : 0;
        ppq += bps * static_cast<double>(kBlock);
    }
    CHECK(noteOns == 30);
}

TEST_CASE("Clip Slice replays every captured note in every bar, also across bar lines", "[engine][slicer]") {
    Engine eng;
    eng.prepare(kSr, kBlock);
    Params p = slicerParams(SlicerStyle::clipSlice);
    p[ParamId::swing] = 0.3f; // beats 0 and 1 are even 16ths: Swing does not move them

    eng.inputNoteOn(36, 118, 0.0); // on the downbeat
    eng.inputNoteOff(36, 0.25);
    eng.inputNoteOn(38, 114, 1.0);
    eng.inputNoteOff(38, 1.25);
    REQUIRE(eng.capturedClipCount() == 2);

    // From beat 4 to just past beat 20. At 140 BPM a 64-sample block is not a whole fraction of a bar,
    // so the blocks straddle the bar lines at 8, 12, 16 and 20.
    std::vector<float> l(kBlock), r(kBlock);
    MidiEventBuffer midi;
    const double bps = 140.0 / 60.0 / kSr;
    double ppq = 4.0;
    int downbeats = 0;
    int backbeats = 0;
    const int blocks = static_cast<int>(std::ceil(16.2 / bps / static_cast<double>(kBlock)));
    for (int blk = 0; blk < blocks; ++blk) {
        eng.process(p, {140.0, ppq, true}, midi, l.data(), r.data(), kBlock);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            if (!ev.isNoteOn())
                continue;
            CHECK((ev.noteNumber() == 36 || ev.noteNumber() == 38));
            downbeats += ev.noteNumber() == 36 ? 1 : 0;
            backbeats += ev.noteNumber() == 38 ? 1 : 0;
        }
        ppq += bps * static_cast<double>(kBlock);
    }
    CHECK(downbeats == 5); // beats 4, 8, 12, 16, 20
    CHECK(backbeats == 4); // beats 5, 9, 13, 17
}

TEST_CASE("Pads in MIDI Slicer mode release every note", "[engine][slicer]") {
    for (int program = 0; program < kPerfPadCount; ++program) {
        Engine eng;
        eng.prepare(kSr, kBlock);
        eng.inputNoteOn(48, 100);
        eng.inputNoteOn(55, 100);
        Params p = slicerParams(SlicerStyle::stabChop);
        p[ParamId::slicerLatch] = 1.0f;
        p[ParamId::sceneQuantize] = static_cast<float>(Quantize::instant);
        p[ParamId::padProgram] = static_cast<float>(program);

        std::vector<float> l(kBlock), r(kBlock);
        MidiEventBuffer midi;
        const double bps = 132.0 / 60.0 / kSr;
        double ppq = 0.0;
        int balance = 0;
        int noteOns = 0;
        for (int blk = 0; blk < 2400; ++blk) {
            p[ParamId::padTouch] = (ppq >= 2.0 && ppq < 6.0) ? 1.0f : 0.0f;
            p[ParamId::padX] = 0.8f;
            eng.process(p, {132.0, ppq, true}, midi, l.data(), r.data(), kBlock);
            for (int i = 0; i < midi.count; ++i) {
                const auto& ev = midi.events[static_cast<std::size_t>(i)];
                balance += ev.isNoteOn() ? 1 : (ev.isNoteOff() ? -1 : 0);
                noteOns += ev.isNoteOn() ? 1 : 0;
            }
            ppq += bps * static_cast<double>(kBlock);
        }
        eng.process(p, {132.0, ppq, false}, midi, l.data(), r.data(), kBlock);
        for (int i = 0; i < midi.count; ++i) {
            const auto& ev = midi.events[static_cast<std::size_t>(i)];
            balance += ev.isNoteOn() ? 1 : (ev.isNoteOff() ? -1 : 0);
        }
        INFO("program " << program);
        CHECK(noteOns > 4);
        CHECK(balance == 0);
        CHECK(eng.activeNotesLowMask() == 0);
    }
}
```

- [x] **Step 2: Write the failing export tests**

Create `tests/engine/SmfTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove_state.h"
#include "cl/hits.h"
#include "cl/resolve.h"
#include "cl/smf.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace cl;

namespace {

struct SmfNote {
    std::uint32_t tick = 0;
    int note = 0;
    int velocity = 0;
};

struct Smf {
    int ppq = 0;
    std::uint32_t endTick = 0;
    std::vector<SmfNote> noteOns;
    int noteOffs = 0;
    int pitchBends = 0;
};

// Reads the one track that encodeSmfType0 writes: full status bytes, a tempo meta event, end of track.
Smf parse(const std::vector<std::uint8_t>& bytes) {
    Smf out;
    REQUIRE(bytes.size() > 22);
    REQUIRE(bytes[0] == 'M');
    REQUIRE(bytes[1] == 'T');
    REQUIRE(bytes[2] == 'h');
    REQUIRE(bytes[3] == 'd');
    out.ppq = (static_cast<int>(bytes[12]) << 8) | static_cast<int>(bytes[13]);
    std::size_t pos = 22;
    std::uint32_t tick = 0;
    while (pos < bytes.size()) {
        std::uint32_t delta = 0;
        while (pos < bytes.size()) {
            const std::uint8_t b = bytes[pos++];
            delta = (delta << 7u) | (b & 0x7Fu);
            if ((b & 0x80u) == 0u)
                break;
        }
        tick += delta;
        REQUIRE(pos < bytes.size());
        const std::uint8_t status = bytes[pos];
        if (status == 0xFFu) {
            const std::uint8_t type = bytes[pos + 1];
            const std::uint8_t length = bytes[pos + 2];
            if (type == 0x2Fu) {
                out.endTick = tick;
                break;
            }
            pos += 3u + length;
            continue;
        }
        const std::uint8_t hi = status & 0xF0u;
        if (hi == 0x90u && bytes[pos + 2] > 0)
            out.noteOns.push_back({tick, bytes[pos + 1], bytes[pos + 2]});
        else if (hi == 0x80u || hi == 0x90u)
            ++out.noteOffs;
        else if (hi == 0xE0u)
            ++out.pitchBends;
        pos += (hi == 0xC0u || hi == 0xD0u) ? 2u : 3u;
    }
    return out;
}

int count(const Smf& smf, int note, std::uint32_t fromTick, std::uint32_t toTick) {
    int n = 0;
    for (const auto& on : smf.noteOns)
        n += (on.note == note && on.tick >= fromTick && on.tick < toTick) ? 1 : 0;
    return n;
}

// Two bars. Kick on 1 and 3, hat on the 8ths, one Detail ghost, and a two-beat snare fill.
GrooveBank exportBank() {
    GrooveBank bank;
    Pattern& p = bank.scenes[0];
    setPatternBars(p, 2);
    p.lanes[2].steps[7].velocity = 56;
    p.lanes[2].steps[7].layer = Layer::detail;
    p.lanes[4].steps[22].ratchet = 2;
    p.fill.beats = 2;
    p.fill.steps[1][8].velocity = 96;
    p.fill.steps[1][10].velocity = 96;
    p.fill.steps[1][12].velocity = 118;
    return bank;
}

} // namespace

TEST_CASE("Export writes the pattern once through at the current Rate", "[smf]") {
    const GrooveBank bank; // default beat, one bar, 12 note-ons
    for (int rate = 0; rate < 3; ++rate) {
        Params p;
        p[ParamId::seqRate] = static_cast<float>(rate);
        const Smf smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 172.0, 0, bank));
        INFO("rate " << rate);
        CHECK(smf.ppq == 960);
        CHECK(smf.endTick == (rate == 0 ? 7680u : (rate == 1 ? 3840u : 1920u)));
        CHECK(smf.noteOns.size() == 12);
        CHECK(smf.noteOffs == 12);
        CHECK(smf.pitchBends == 0);
        for (const auto& on : smf.noteOns)
            CHECK(on.tick < smf.endTick);
    }
}

TEST_CASE("Export plays what resolveBar reports, with Density and Dynamics applied", "[smf]") {
    const GrooveBank bank = exportBank();
    const GrooveState groove(bank);
    for (const float density : {0.0f, 0.5f, 1.0f}) {
        Params p;
        p[ParamId::density] = density;
        p[ParamId::dynamics] = 0.8f;
        p[ParamId::noteMap] = static_cast<float>(NoteMap::chromatic);
        const Smf smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 140.0, 0, bank));

        int expected = 0;
        for (int bar = 0; bar < 2; ++bar) {
            ResolvedBar resolved;
            resolveBar(groove.bank.scenes[0], bar, {density, 0.8f, false}, resolved);
            for (const auto& lane : resolved.lanes)
                for (const auto& s : lane)
                    expected += s.velocity > 0 ? static_cast<int>(s.ratchet) + (s.flam ? 1 : 0) : 0;
        }
        INFO("density " << density);
        CHECK(static_cast<int>(smf.noteOns.size()) == expected);
        CHECK(count(smf, 38, 0, smf.endTick) == (density >= 1.0f ? 1 : 0)); // the Detail ghost, chromatic lane 2
        for (const auto& on : smf.noteOns) {
            if (on.note == 36)
                CHECK(on.velocity == applyDynamics(96, 0.8f));
        }
    }
}

TEST_CASE("The last bar of an export has the fill when Fill is not Off", "[smf]") {
    const GrooveBank bank = exportBank();
    Params p;
    p[ParamId::noteMap] = static_cast<float>(NoteMap::chromatic); // snare is note 37
    Smf smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 140.0, 0, bank));
    CHECK(smf.endTick == 7680u);
    CHECK(count(smf, 37, 3840u, 7680u) == 2); // the main pattern's backbeats in bar 2

    p[ParamId::fillEvery] = 2.0f;
    smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 140.0, 0, bank));
    CHECK(count(smf, 37, 0u, 3840u) == 2);               // bar 1 is untouched
    CHECK(count(smf, 37, 3840u, 3840u + 1920u) == 1);    // beat 2 of bar 2, before the fill window
    CHECK(count(smf, 37, 3840u + 1920u, 7680u) == 3);    // the drawn fill replaces the snare lane's last two beats
    CHECK(count(smf, 36, 3840u + 1920u, 7680u) == 1);    // the kick draws nothing in the fill and plays through
}

TEST_CASE("A hit before bar 1 or after the last bar wraps to the other end of the clip", "[smf]") {
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    bank.scenes[0].lanes[0].steps[0].velocity = 96;
    bank.scenes[0].lanes[0].steps[0].layer = Layer::core;
    bank.scenes[0].lanes[0].steps[0].flam = true; // grace hit 20 ms before beat 0
    bank.scenes[0].lanes[1].steps[15].velocity = 96;
    bank.scenes[0].lanes[1].steps[15].layer = Layer::core;
    bank.scenes[0].lanes[1].steps[15].nudge = 24; // lands on the next bar's downbeat
    Params p;
    const Smf smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 120.0, 0, bank));
    REQUIRE(smf.noteOns.size() == 3);
    CHECK(count(smf, 36, 0u, 1u) == 1);       // the kick itself
    CHECK(count(smf, 36, 3780u, 3840u) == 1); // its grace hit: 0.04 beats before the loop point
    CHECK(count(smf, 38, 0u, 20u) == 1);      // the late snare, wrapped to the start
    CHECK(smf.noteOffs == 3);
}

TEST_CASE("Two lanes that share a note on the same step export one note, the louder one", "[smf]") {
    GrooveBank bank;
    bank.scenes[0] = Pattern{};
    bank.scenes[0].lanes[0].steps[0].velocity = 96; // kick
    bank.scenes[0].lanes[0].steps[0].layer = Layer::core;
    bank.scenes[0].lanes[1].steps[0].velocity = 118; // snare: the Strike Body map plays both as note 36
    bank.scenes[0].lanes[1].steps[0].layer = Layer::core;
    Params p;
    p[ParamId::noteMap] = static_cast<float>(NoteMap::strikeBody);
    const Smf smf = parse(exportSmf(p, defaultLaneCurves(), HeldNotes{}, 120.0, 0, bank));
    REQUIRE(smf.noteOns.size() == 1);
    CHECK(smf.noteOns[0].note == 36);
    CHECK(smf.noteOns[0].velocity == 118);
    CHECK(smf.noteOffs == 1);
}

TEST_CASE("Slicer export writes the compiled phrase, and Clip Slice writes no notes", "[smf]") {
    const GrooveBank bank;
    HeldNotes held;
    held.push(48, 100);
    held.push(55, 100);
    Params p;
    p[ParamId::engineMode] = static_cast<float>(EngineMode::slicer);
    Smf smf = parse(exportSmf(p, defaultLaneCurves(), held, 120.0, 0, bank));
    CHECK(smf.endTick == 3840u);
    CHECK(smf.noteOns.size() == 10); // five lit gate steps, two notes each
    for (const auto& on : smf.noteOns)
        CHECK((on.note == 48 || on.note == 55));

    p[ParamId::slicerStyle] = static_cast<float>(SlicerStyle::clipSlice);
    smf = parse(exportSmf(p, defaultLaneCurves(), held, 120.0, 0, bank));
    CHECK(smf.noteOns.empty());
    CHECK(smf.endTick == 3840u);
}
```

In `tests/CMakeLists.txt`, add `engine/SlicerTests.cpp` and `engine/SmfTests.cpp` to the `cl-engine-tests` source list, after `engine/PadTests.cpp`.

In `engine/CMakeLists.txt`, add `include/cl/phrase.h`, `include/cl/smf.h`, `src/phrase.cpp` and `src/smf.cpp` to the `cl_engine` source list, in alphabetical position.

- [x] **Step 3: Run them to verify they fail**

```bash
cmake --build --preset engine --target cl-engine-tests
```

Expected: the build fails. `phrase.cpp` still holds the 2.0 compiler, so the first errors name `styles.h` or `PhrasePlan`.

- [x] **Step 4: Rewrite the slicer compiler**

Replace `engine/include/cl/phrase.h` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// The MIDI Slicer compiler: held notes and the literal gate strip in, one phrase of notes out.
// The phrase is as long as the active scene's pattern. Nothing in it is random.
#pragma once

#include "cl/groove_state.h"
#include "cl/held_notes.h"
#include "cl/nonblocking.h"
#include "cl/params.h"

#include <array>
#include <cstdint>

namespace cl {

struct SlicerNote {
    double beat = 0.0; // start, in pattern beats from the start of the phrase
    double durBeats = 0.2;
    std::int8_t note = 60;
    std::int8_t voice = 0;  // which held note produced it, lowest first (capped at 7)
    std::int16_t step = 0;  // the gate step that produced it
    std::uint8_t velocity = 100;
};

inline constexpr int kMaxSlicerNotes = 768;

struct SlicerPlan {
    double phraseBeats = 4.0;
    int noteCount = 0;
    std::array<SlicerNote, kMaxSlicerNotes> notes{};
};

// Compiles the slicer phrase for `scene`, sorted by start time. Stab Chop, Strum / Flam, Break Arp and
// Bass / Stab produce notes. Clip Slice and an empty `held` produce none: Clip Slice replays captured
// notes inside the engine.
void compileSlicer(const Params& params, const GrooveState& groove, int scene, const HeldNotes& held, double bpm,
                   SlicerPlan& out) noexcept CL_NONBLOCKING;

// Everything a compiled phrase depends on besides the bank's contents, which callers track with a
// revision counter. The engine and the UI model both keep the inputs of their last compile and
// recompile only when sameAs() says something changed. Compared field by field; nothing is hashed.
struct SlicerInputs {
    bool valid = false; // false until the first compile
    std::uint32_t grooveRevision = 0;
    int scene = 0;
    int style = 0;
    int rateIdx = 0;
    float density = 0.0f;
    float dynamics = 0.0f;
    float swing = 0.0f;
    float gateLen = 0.0f;
    float strumMs = 0.0f;
    double bpm = 0.0;
    HeldNotes held{};

    bool sameAs(const SlicerInputs& other) const noexcept CL_NONBLOCKING;
};

SlicerInputs slicerInputs(const Params& params, int scene, const HeldNotes& held, double bpm,
                          std::uint32_t grooveRevision) noexcept CL_NONBLOCKING;

} // namespace cl
```

Replace `engine/src/phrase.cpp` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/phrase.h"

#include "cl/hits.h"
#include "cl/resolve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cl {

namespace {

void pushNote(SlicerPlan& out, const SlicerNote& note) noexcept CL_NONBLOCKING {
    if (out.noteCount < kMaxSlicerNotes)
        out.notes[static_cast<std::size_t>(out.noteCount++)] = note;
}

void sortNotes(SlicerPlan& out) noexcept CL_NONBLOCKING {
    for (int i = 1; i < out.noteCount; ++i) {
        const SlicerNote key = out.notes[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && out.notes[static_cast<std::size_t>(j)].beat > key.beat) {
            out.notes[static_cast<std::size_t>(j + 1)] = out.notes[static_cast<std::size_t>(j)];
            --j;
        }
        out.notes[static_cast<std::size_t>(j + 1)] = key;
    }
}

} // namespace

void compileSlicer(const Params& params, const GrooveState& groove, int scene, const HeldNotes& held, double bpm,
                   SlicerPlan& out) noexcept CL_NONBLOCKING {
    const auto sceneIdx = static_cast<std::size_t>(std::clamp(scene, 0, kSceneCount - 1));
    const Pattern& pattern = groove.bank.scenes[sceneIdx];
    const int totalSteps = patternSteps(pattern);
    const double totalBeats = static_cast<double>(totalSteps) * 0.25;
    out.phraseBeats = totalBeats;
    out.noteCount = 0;

    const auto style = static_cast<SlicerStyle>(std::clamp(params.choice(ParamId::slicerStyle), 0, 4));
    if (style == SlicerStyle::clipSlice || held.count <= 0)
        return;

    const double swing = static_cast<double>(params[ParamId::swing]);
    const float dynamics = params[ParamId::dynamics];
    const double gateLen = static_cast<double>(std::clamp(params[ParamId::gateLen], 0.05f, 1.0f));
    const auto rateIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::seqRate), 0, 2));
    const double beatsPerMs = std::clamp(bpm, 20.0, 400.0) / 60000.0 * kSeqRateMultipliers[rateIdx];
    const double strumBeats = static_cast<double>(std::clamp(params[ParamId::strumMs], 0.0f, 45.0f)) * beatsPerMs;
    const int gateLength = std::clamp(static_cast<int>(groove.bank.slicerLength), 1, kMaxSlicerSteps);
    const auto& gate = groove.bank.slicerSteps;

    ResolvedBar kickBar;
    int resolvedBar = -1;
    int arpCursor = 0;

    for (int s16 = 0; s16 < totalSteps; ++s16) {
        const int gateIdx = s16 % gateLength;
        const Step& step = gate[static_cast<std::size_t>(gateIdx)];
        const double start = swingBeat(static_cast<double>(s16) * 0.25, swing);
        const double next = swingBeat(static_cast<double>(s16 + 1) * 0.25, swing);

        if (style == SlicerStyle::bassStab) {
            // The lowest held note follows the kick lane of the resolved bar; the rest play the gate.
            const int bar = s16 / kStepsPerBar;
            if (bar != resolvedBar) {
                resolveBar(pattern, groove.maps[sceneIdx], bar, {params[ParamId::density], dynamics, false}, kickBar);
                resolvedBar = bar;
            }
            const double at = std::clamp(start, 0.0, totalBeats - 0.03);
            if (kickBar.lanes[0][static_cast<std::size_t>(s16 % kStepsPerBar)].velocity > 0) {
                SlicerNote bass;
                bass.beat = at;
                bass.durBeats = std::min(0.16 + 0.34 * gateLen, totalBeats - at - 0.01);
                bass.note = static_cast<std::int8_t>(held.notes[0]);
                bass.voice = 0;
                bass.step = static_cast<std::int16_t>(s16 % kStepsPerBar);
                bass.velocity = applyDynamics(static_cast<int>(held.velocities[0]), dynamics);
                pushNote(out, bass);
            }
            if (step.on() && (s16 % kStepsPerBar) != 0 && held.count > 1) {
                const double stabDur = std::min(0.10 + 0.32 * gateLen, totalBeats - at - 0.01);
                for (int vi = 1; vi < held.count; ++vi) {
                    const double startAt =
                        std::min(totalBeats - 0.02, at + static_cast<double>(vi - 1) * strumBeats * 0.45);
                    SlicerNote stab;
                    stab.beat = startAt;
                    stab.durBeats = std::max(0.03, std::min(stabDur, totalBeats - startAt - 0.01));
                    stab.note = static_cast<std::int8_t>(held.notes[static_cast<std::size_t>(vi)]);
                    stab.voice = static_cast<std::int8_t>(std::min(vi, 7));
                    stab.step = static_cast<std::int16_t>(gateIdx);
                    const int base = static_cast<int>(held.velocities[static_cast<std::size_t>(vi)]) -
                                     (held.count > 3 && vi == 1 ? 8 : 0);
                    stab.velocity = applyDynamics(base, dynamics);
                    pushNote(out, stab);
                }
            }
            continue;
        }

        if (!step.on())
            continue;

        const double at = std::clamp(start + static_cast<double>(step.nudge) / 96.0, 0.0, totalBeats - 0.03);

        // The gate length scales with the distance to the next lit step.
        int gapSteps = 1;
        for (int g = 1; g < 8 && (s16 + g) < totalSteps; ++g) {
            if (gate[static_cast<std::size_t>((s16 + g) % gateLength)].on())
                break;
            ++gapSteps;
        }
        const double baseDur = std::min(static_cast<double>(gapSteps) * 0.25 * (0.18 + 0.80 * gateLen),
                                        std::max(0.04, totalBeats - at - 0.01));

        const int reps = std::clamp(static_cast<int>(step.ratchet), 1, 4);
        const double subStep = (next - start) / static_cast<double>(reps);

        if (style == SlicerStyle::breakArp) {
            const int voice = arpCursor % held.count;
            ++arpCursor;
            for (int r = 0; r < reps; ++r) {
                const double startAt = at + static_cast<double>(r) * subStep;
                if (startAt >= totalBeats - 0.02)
                    break;
                SlicerNote n;
                n.beat = startAt;
                n.durBeats = reps > 1 ? std::min(subStep * 0.78, totalBeats - startAt - 0.01)
                                      : std::min(baseDur, totalBeats - startAt - 0.01);
                n.note = static_cast<std::int8_t>(held.notes[static_cast<std::size_t>(voice)]);
                n.voice = static_cast<std::int8_t>(std::min(voice, 7));
                n.step = static_cast<std::int16_t>(gateIdx);
                n.velocity = applyDynamics(static_cast<int>(step.velocity), dynamics);
                pushNote(out, n);
            }
            continue;
        }

        // Stab Chop, or Strum / Flam: every held note. A flam on the step strums it.
        const bool strum = (style == SlicerStyle::strumFlam) || step.flam;
        const bool reverse = strum && ((s16 / 4) & 1) != 0;
        for (int r = 0; r < reps; ++r) {
            const double base = at + static_cast<double>(r) * subStep;
            if (base >= totalBeats - 0.02)
                break;
            for (int vi = 0; vi < held.count; ++vi) {
                const int order = reverse ? (held.count - 1 - vi) : vi;
                const double startAt =
                    std::min(totalBeats - 0.02, base + (strum ? static_cast<double>(order) * strumBeats : 0.0));
                const double dur = reps > 1 ? std::min(subStep * 0.78, totalBeats - startAt - 0.01)
                                            : std::min(baseDur, totalBeats - startAt - 0.01);
                const int velocity =
                    (static_cast<int>(step.velocity) + static_cast<int>(held.velocities[static_cast<std::size_t>(vi)])) /
                        2 +
                    (strum && vi == held.count - 1 ? 8 : 0);
                SlicerNote n;
                n.beat = startAt;
                n.durBeats = std::max(0.03, dur);
                n.note = static_cast<std::int8_t>(held.notes[static_cast<std::size_t>(vi)]);
                n.voice = static_cast<std::int8_t>(std::min(vi, 7));
                n.step = static_cast<std::int16_t>(gateIdx);
                n.velocity = applyDynamics(velocity, dynamics);
                pushNote(out, n);
            }
        }
    }

    sortNotes(out);
}

bool SlicerInputs::sameAs(const SlicerInputs& other) const noexcept CL_NONBLOCKING {
    return valid && other.valid && grooveRevision == other.grooveRevision && scene == other.scene &&
           style == other.style && rateIdx == other.rateIdx && density == other.density && dynamics == other.dynamics &&
           swing == other.swing && gateLen == other.gateLen && strumMs == other.strumMs && bpm == other.bpm &&
           held.sameAs(other.held);
}

SlicerInputs slicerInputs(const Params& params, int scene, const HeldNotes& held, double bpm,
                          std::uint32_t grooveRevision) noexcept CL_NONBLOCKING {
    SlicerInputs in;
    in.valid = true;
    in.grooveRevision = grooveRevision;
    in.scene = scene;
    in.style = params.choice(ParamId::slicerStyle);
    in.rateIdx = params.choice(ParamId::seqRate);
    in.density = params[ParamId::density];
    in.dynamics = params[ParamId::dynamics];
    in.swing = params[ParamId::swing];
    in.gateLen = params[ParamId::gateLen];
    in.strumMs = params[ParamId::strumMs];
    in.bpm = bpm;
    in.held = held;
    return in;
}

} // namespace cl
```

- [x] **Step 5: Play the slicer and the captured clip in the engine**

In `engine/include/cl/engine.h`, add `#include "cl/phrase.h"` and, after `position()`:

```cpp
    const SlicerPlan& slicerPlan() const noexcept CL_NONBLOCKING;
```

In `engine/src/engine_impl.h`, add `#include "cl/phrase.h"`, and inside `struct Engine::Impl` add:

```cpp
    SlicerPlan slicerPlanData{};
    SlicerInputs slicerKey{}; // what slicerPlanData was last compiled from
```

and these declarations:

```cpp
    void pushNoteHit(int note, int voice, double hostBeat, double hostDur, int velocity) noexcept CL_NONBLOCKING;
    void ensureSlicerPlan(const Params& params, double bpm) noexcept CL_NONBLOCKING;
    void playSlicer(const Block& b) noexcept CL_NONBLOCKING;
    void playClip(const Block& b) noexcept CL_NONBLOCKING;
```

In `Engine::Impl::reset()` add `slicerKey = {};` and `slicerPlanData.noteCount = 0;`.

In `engine/src/engine.cpp` add:

```cpp
void Engine::Impl::pushNoteHit(int note, int voice, double hostBeat, double hostDur,
                               int velocity) noexcept CL_NONBLOCKING {
    if (blockHitCount >= kMaxBlockHits)
        return;
    BlockHit& hit = blockHits[static_cast<std::size_t>(blockHitCount++)];
    hit.beat = hostBeat;
    hit.durBeats = hostDur;
    hit.note = static_cast<std::int8_t>(std::clamp(note, 0, 127));
    hit.lane = static_cast<std::int8_t>(std::clamp(voice, 0, 7));
    hit.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
    hit.drum = false;
}

// Recompiles the slicer phrase only when something it depends on changed.
void Engine::Impl::ensureSlicerPlan(const Params& params, double bpm) noexcept CL_NONBLOCKING {
    const SlicerInputs now = slicerInputs(params, currentScene, lastEffectiveHeld, bpm, grooveRevision);
    if (now.sameAs(slicerKey))
        return;
    compileSlicer(params, groove, currentScene, lastEffectiveHeld, bpm, slicerPlanData);
    slicerKey = now;
}

// The compiled phrase, looping at the pattern's length.
void Engine::Impl::playSlicer(const Block& b) noexcept CL_NONBLOCKING {
    const double phraseBeats = std::max(1.0, slicerPlanData.phraseBeats);
    const double scaledEnd = b.windowEnd * b.rate;
    double scan = b.windowStart * b.rate;
    for (int pass = 0; pass < 3 && scan < scaledEnd - kBeatEps; ++pass) {
        const double origin = std::floor(scan / phraseBeats) * phraseBeats;
        const double phraseEnd = origin + phraseBeats;
        const double subEnd = std::min(scaledEnd, phraseEnd);
        for (int i = 0; i < slicerPlanData.noteCount; ++i) {
            const SlicerNote& n = slicerPlanData.notes[static_cast<std::size_t>(i)];
            const double at = origin + n.beat;
            if (at < scan - kBeatEps || at >= subEnd - kBeatEps)
                continue;
            pushNoteHit(n.note, n.voice, at / b.rate, std::min(n.durBeats, phraseEnd - at) / b.rate, n.velocity);
        }
        scan = subEnd;
    }
}

// Clip Slice: replays the notes captured from the MIDI input, bar by bar, with Swing.
// While Repeat is engaged it loops the captured notes inside the Repeat window instead.
// A block can straddle a bar line (or a Repeat cell), so every bar and cell the block touches is visited:
// working from the block's start alone would skip a captured note that sits exactly on the downbeat.
void Engine::Impl::playClip(const Block& b) noexcept CL_NONBLOCKING {
    const double swing = static_cast<double>((*b.params)[ParamId::swing]);
    const double scaledStart = b.windowStart * b.rate;
    const double scaledEnd = b.windowEnd * b.rate;
    const bool repeating = engaged && b.program == PadProgram::repeat;
    const double window = pads::repeatWindowBeats(b.padX);
    const double anchorInBar = repeatAnchor - std::floor(repeatAnchor / 4.0) * 4.0;
    const double sliceStart = std::min(anchorInBar, 4.0 - window);
    const int cellsPerBar = static_cast<int>(std::lround(4.0 / window));

    const auto firstBar = static_cast<std::int64_t>(std::floor(scaledStart / 4.0));
    const auto lastBar = static_cast<std::int64_t>(std::floor((scaledEnd - kBeatEps) / 4.0));
    for (std::int64_t bar = firstBar; bar <= lastBar && bar < firstBar + kMaxBarsPerBlock; ++bar) {
        const double barOrigin = static_cast<double>(bar) * 4.0;
        const auto emitAt = [&](const CapturedEvent& ev, double barBeat) noexcept CL_NONBLOCKING {
            const double at = barOrigin + swingBeat(barBeat, swing);
            if (at < scaledStart - kBeatEps || at >= scaledEnd - kBeatEps)
                return;
            pushNoteHit(ev.note, 0, at / b.rate, std::max(0.04, ev.durBeats / b.rate), static_cast<int>(ev.vel));
        };
        for (const auto& ev : captured) {
            if (!ev.active)
                continue;
            if (!repeating) {
                emitAt(ev, ev.barBeat);
                continue;
            }
            if (ev.barBeat < sliceStart || ev.barBeat >= sliceStart + window)
                continue;
            // Every cell of the bar replays the slice. Visit the cells this block can reach, with one
            // cell of margin on each side because Swing moves a note by up to an eighth of a beat.
            const double offset = ev.barBeat - sliceStart;
            const int firstCell =
                std::max(0, static_cast<int>(std::floor((scaledStart - barOrigin - offset) / window)) - 1);
            const int lastCell = std::min(cellsPerBar - 1,
                                          static_cast<int>(std::floor((scaledEnd - barOrigin - offset) / window)) + 1);
            for (int cell = firstCell; cell <= lastCell; ++cell)
                emitAt(ev, static_cast<double>(cell) * window + offset);
        }
    }
}
```

In `process`, replace

```cpp
    // MIDI Slicer playback is added in Task 4.
```

with an `else` branch on the `if (isDrum) { ... }` above it:

```cpp
    else {
        const bool silent = engaged && b.program == PadProgram::drop && pads::dropIsSilence(padX);
        if (slicerStyle == SlicerStyle::clipSlice) {
            if (engaged && b.program == PadProgram::roll)
                playRoll(b);
            else if (!silent)
                playClip(b);
        } else {
            ensureSlicerPlan(params, bpm);
            if (engaged && b.program == PadProgram::roll)
                playRoll(b);
            else if (engaged && b.program == PadProgram::repeat)
                playRepeat(b);
            else if (!silent)
                playSlicer(b);
        }
    }
```

At the bottom of the file add:

```cpp
const SlicerPlan& Engine::slicerPlan() const noexcept CL_NONBLOCKING { return impl_->slicerPlanData; }
```

In `engine/src/pad_programs.cpp`, give Repeat and Roll their slicer halves. At the top of `playRepeat`, after `window` and `gate` are computed, add:

```cpp
    if (!b.isDrum) {
        // MIDI Slicer: loop the phrase's notes that start inside the window.
        const double phraseBeats = std::max(1.0, slicerPlanData.phraseBeats);
        const double local = repeatAnchor - std::floor(repeatAnchor / phraseBeats) * phraseBeats;
        const double from = std::min(local, phraseBeats - window);
        const double winStart = b.windowStart * b.rate;
        const double winEnd = b.windowEnd * b.rate;
        const auto first =
            std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor((winStart - repeatAnchor) / window)));
        const auto last = static_cast<std::int64_t>(std::floor((winEnd - repeatAnchor) / window));
        for (std::int64_t cycle = first; cycle <= last && cycle < first + 64; ++cycle) {
            const double origin = repeatAnchor + static_cast<double>(cycle) * window;
            for (int i = 0; i < slicerPlanData.noteCount; ++i) {
                const SlicerNote& n = slicerPlanData.notes[static_cast<std::size_t>(i)];
                const double offset = n.beat - from;
                if (offset < 0.0 || offset >= window)
                    continue;
                const double at = origin + offset;
                if (at < winStart - kBeatEps || at >= winEnd - kBeatEps)
                    continue;
                const double dur = std::min(n.durBeats, window * 0.85) * gate;
                pushNoteHit(n.note, n.voice, at / b.rate, std::max(0.025, dur / b.rate), n.velocity);
            }
        }
        pos.repeatStartStep = static_cast<int>(std::lround(from * 4.0));
        pos.repeatBeats = window;
        return;
    }
```

In `playRoll`, replace `if (b.isDrum) pushDrumHit(...)` with:

```cpp
        if (b.isDrum) {
            pushDrumHit(b, 1, beat, step * 0.8, velocity);
        } else {
            // MIDI Slicer: retrigger the held notes.
            for (int i = 0; i < lastEffectiveHeld.count; ++i)
                pushNoteHit(lastEffectiveHeld.notes[static_cast<std::size_t>(i)], i, beat, step * 0.8, velocity);
        }
```

- [x] **Step 6: Rewrite MIDI export**

Replace `engine/include/cl/smf.h` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// 960-PPQ Standard MIDI File (Type 0) export. Drum notes come from resolveBar and forEachHit,
// the same two functions the engine plays from.
#pragma once

#include "cl/groove.h"
#include "cl/held_notes.h"
#include "cl/lanes.h"
#include "cl/params.h"

#include <cstdint>
#include <vector>

namespace cl {

inline constexpr std::uint16_t kSmfPpq = 960;

// Writes `scene`'s pattern once through, bar 1 to the last bar, with the current Density, Dynamics,
// Swing and Rate. The clip is `bars * 4 / rate` beats long. The last bar includes the fill when Fill
// is not Off. In MIDI Slicer mode it writes the compiled slicer phrase for `held` instead.
std::vector<std::uint8_t> exportSmf(const Params& params, const LaneCurves& curves, const HeldNotes& held, double bpm,
                                    int scene, const GrooveBank& bank);

} // namespace cl
```

Replace `engine/src/smf.cpp` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/smf.h"

#include "cl/groove_state.h"
#include "cl/hits.h"
#include "cl/pattern.h"
#include "cl/phrase.h"
#include "cl/resolve.h"

#include <hba/core/smf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace cl {

namespace {
int toMidi7(double bipolar) { return std::clamp(static_cast<int>(std::lround((bipolar * 0.5 + 0.5) * 127.0)), 0, 127); }

struct ClipNote {
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};
} // namespace

std::vector<std::uint8_t> exportSmf(const Params& params, const LaneCurves& curves, const HeldNotes& held, double bpm,
                                    int scene, const GrooveBank& bank) {
    const double safeBpm = std::clamp(bpm, 20.0, 400.0);
    const auto rateIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::seqRate), 0, 2));
    const double rate = kSeqRateMultipliers[rateIdx];
    const auto sceneIdx = static_cast<std::size_t>(std::clamp(scene, 0, kSceneCount - 1));
    const GrooveState groove(bank);
    const Pattern& pattern = groove.bank.scenes[sceneIdx];
    const int bars = patternBars(pattern);
    const double clipBeats = static_cast<double>(bars) * 4.0 / rate; // host beats
    const auto clipTicks = static_cast<long long>(std::llround(clipBeats * kSmfPpq));
    const auto mode = static_cast<EngineMode>(std::clamp(params.choice(ParamId::engineMode), 0, 1));
    const auto noteMap = static_cast<NoteMap>(std::clamp(params.choice(ParamId::noteMap), 0, kNoteMapCount - 1));

    auto evalSends = [&](double beat) {
        std::array<float, kLaneCount> sends{};
        for (int l = 0; l < kLaneCount; ++l)
            sends[static_cast<std::size_t>(l)] = static_cast<float>(laneSend(params, curves, l, beat));
        return sends;
    };
    auto targetActive = [&](LaneTarget target) {
        for (int l = 0; l < kLaneCount; ++l) {
            if (params.on(laneParam(l, LaneField::on)) && laneTargetOf(params, l) == target)
                return true;
        }
        return false;
    };

    std::vector<ClipNote> notes;
    // `patternBeat` may be negative (a flam, an early nudge or lane offset on bar 1) or past the last bar
    // (a late nudge on the last step). The plugin loops the pattern, so such a hit belongs at the other
    // end of the clip: wrap it.
    const auto add = [&](int note, double patternBeat, double durBeats, int velocity) {
        long long tick = std::llround(patternBeat / rate * kSmfPpq);
        tick = ((tick % clipTicks) + clipTicks) % clipTicks;
        const long long startTick = std::min(tick, clipTicks - 16);
        const long long length = std::llround(std::max(0.025, durBeats / rate) * kSmfPpq);
        const long long endTick = std::clamp(startTick + length, startTick + 12, clipTicks - 1);
        const double hostBeat = static_cast<double>(startTick) / kSmfPpq;
        const int bias =
            static_cast<int>(std::lround(laneSum(params, evalSends(hostBeat), LaneTarget::velocity) * 26.0f));
        notes.push_back({static_cast<std::uint8_t>(std::clamp(note, 0, 127)),
                         static_cast<std::uint8_t>(std::clamp(velocity + bias, 1, 127)),
                         static_cast<std::uint32_t>(startTick), static_cast<std::uint32_t>(endTick)});
    };

    if (mode == EngineMode::drum) {
        const HitTiming timing{static_cast<double>(params[ParamId::swing]), static_cast<double>(params[ParamId::gateLen]),
                               safeBpm / 60000.0 * rate};
        const bool fillOn = params.choice(ParamId::fillEvery) != 0;
        for (int bar = 0; bar < bars; ++bar) {
            ResolvedBar resolved;
            resolveBar(pattern, groove.maps[sceneIdx], bar,
                       {params[ParamId::density], params[ParamId::dynamics], fillOn && bar == bars - 1}, resolved);
            forEachHit(pattern, resolved, static_cast<double>(bar) * 4.0, timing, [&](const Hit& h) {
                const int note =
                    effectiveLaneMidiNote(noteMap, h.lane, pattern.lanes[static_cast<std::size_t>(h.lane)].midiNote);
                add(note, h.beat, h.durBeats, h.velocity);
            });
        }
    } else {
        const auto plan = std::make_unique<SlicerPlan>();
        compileSlicer(params, groove, static_cast<int>(sceneIdx), held, safeBpm, *plan);
        for (int i = 0; i < plan->noteCount; ++i) {
            const SlicerNote& n = plan->notes[static_cast<std::size_t>(i)];
            add(n.note, n.beat, n.durBeats, n.velocity);
        }
    }

    std::stable_sort(notes.begin(), notes.end(),
                     [](const ClipNote& a, const ClipNote& b) { return a.start < b.start; });

    // Two lanes can share a MIDI note (the Strike Body map does). A pitch can only start once per tick:
    // keep one note, with the higher velocity and the later end.
    std::vector<ClipNote> merged;
    merged.reserve(notes.size());
    for (const ClipNote& n : notes) {
        bool duplicate = false;
        for (auto it = merged.rbegin(); it != merged.rend() && it->start == n.start; ++it) {
            if (it->note != n.note)
                continue;
            it->velocity = std::max(it->velocity, n.velocity);
            it->end = std::max(it->end, n.end);
            duplicate = true;
            break;
        }
        if (!duplicate)
            merged.push_back(n);
    }
    notes = std::move(merged);

    // A note ends when the same pitch starts again.
    for (std::size_t i = 0; i < notes.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (notes[j].note == notes[i].note && notes[j].end > notes[i].start)
                notes[j].end = notes[i].start > notes[j].start ? notes[i].start : notes[j].start + 1U;
        }
    }

    const auto endTick = static_cast<std::uint32_t>(clipTicks);
    std::vector<hba::core::SmfEvent> events;
    events.reserve(notes.size() * 2 + 256);
    for (const auto& n : notes) {
        events.push_back(hba::core::SmfEvent::msg(n.start, 20, 0x90, n.note, n.velocity));
        events.push_back(hba::core::SmfEvent::msg(n.end, 18, 0x80, n.note, 0));
    }

    // Expression lanes as CC and aftertouch. Pitch bend is written only if a lane targets Pitch Scoop,
    // and this exporter never writes it.
    constexpr std::array<std::pair<LaneTarget, int>, 4> kCcMap{{
        {LaneTarget::cc74Slide, 74},
        {LaneTarget::cc1Mod, 1},
        {LaneTarget::cc11Expr, 11},
        {LaneTarget::cc71Res, 71},
    }};
    std::array<int, 4> lastCc{-1, -1, -1, -1};
    int lastAt = -1;
    const bool hasAt = targetActive(LaneTarget::pressure);
    for (double beat = 0.0; beat < clipBeats; beat += 0.125) {
        const auto tick = static_cast<std::uint32_t>(
            std::min<long long>(std::llround(beat * kSmfPpq), static_cast<long long>(endTick) - 1LL));
        const auto sends = evalSends(beat);
        for (std::size_t k = 0; k < kCcMap.size(); ++k) {
            if (!targetActive(kCcMap[k].first))
                continue;
            const int v = toMidi7(std::clamp(static_cast<double>(laneSum(params, sends, kCcMap[k].first)), -1.0, 1.0));
            if (lastCc[k] != v) {
                events.push_back(hba::core::SmfEvent::msg(tick, 25, 0xB0, static_cast<std::uint8_t>(kCcMap[k].second),
                                                          static_cast<std::uint8_t>(v)));
                lastCc[k] = v;
            }
        }
        if (hasAt) {
            const int v =
                toMidi7(std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::pressure)), -1.0, 1.0));
            if (lastAt != v) {
                events.push_back(hba::core::SmfEvent::msg2(tick, 26, 0xD0, static_cast<std::uint8_t>(v)));
                lastAt = v;
            }
        }
    }

    return hba::core::encodeSmfType0(std::move(events), kSmfPpq, safeBpm, endTick);
}

} // namespace cl
```

- [x] **Step 7: Update the command-line renderer**

In `tools/cutline_render.cpp`:

- Change the header comment's "Cutline 2.0" to "Cutline 3".
- Add `#include "cl/groove_state.h"` and `#include <algorithm>`.
- Replace the `exportPhraseSmf(...)` call with:

```cpp
        const auto bytes =
            cl::exportSmf(preset.params, preset.curves, held, bpm, preset.params.choice(cl::ParamId::scene), preset.bank);
```

- Replace `engine.setPatternBank(preset.bank);` with `engine.setGroove(cl::GrooveState(preset.bank));`.

- [x] **Step 8: Delete the 2.0 generator**

```bash
git rm engine/src/styles.cpp engine/src/styles.h tools/seed_patterns.py
```

In `core/include/cl/lanes.h`, delete the line `using hba::dsp::laneHash;`. Nothing in Cutline calls it any more. (The other three using-declarations there are used by the expression lanes and stay.) Rebuild `cl-core-tests` to confirm.

Confirm `engine/CMakeLists.txt` no longer lists `src/styles.cpp` or `src/styles.h` (Task 3 removed them).

- [x] **Step 9: Build and test the whole engine preset**

```bash
cmake --build --preset engine && ctest --preset engine 2>&1 | tail -3
```

Expected: `100% tests passed, 0 tests failed out of 86`. (24 core, 5 shared DSP, 14 resolve, 10 timing, 1 lint, 8 engine, 8 pad, 11 slicer, 5 export, as the plan was first written. Tasks 3 and 4 gained tests during execution, so the real number is higher.) If your count differs because Catch2 names a case differently, report the count; the pass rate is what matters.

- [x] **Step 10: Render the default beat and count its notes**

```bash
build/engine/tools/cutline-render --mid build/engine/default-beat.mid --bpm 172 && python3 - <<'EOF'
data = open('build/engine/default-beat.mid', 'rb').read()
print('bytes', len(data), 'note-ons', sum(1 for i in range(22, len(data) - 2) if data[i] == 0x90 and data[i + 2] > 0))
EOF
```

Expected: a line starting `Wrote`, then `bytes` around 150 and `note-ons 12` (the byte scan can over-count by one or two if a delta-time byte happens to be `0x90`; 12 to 14 is fine). This is the calm default: 12 notes in one bar, where the 2.0 presets had a median of 26.

- [x] **Step 11: Confirm the 2.0 generator and all randomness are gone**

```bash
grep -rnE "laneHash|Rng|uniform\(|warpBeat|compilePhrase|styleKit|euclid|PhrasePlan|appendDrumFill" core engine tools | grep -v "engine/src/audition.h"; test ! -e engine/src/styles.cpp && test ! -e engine/src/styles.h && test ! -e tools/seed_patterns.py && echo deleted
```

Expected: only the line `deleted`. (`engine/src/audition.h` uses `hba::dsp` noise for the hat and snare sounds; if grep prints a line from that file, it is the synth's noise source and is allowed. Any other line is a leftover.)

- [x] **Step 12: Check formatting and commit**

```bash
xcrun clang-format --style=file:../../.clang-format --dry-run --Werror \
  engine/include/cl/phrase.h engine/src/phrase.cpp engine/include/cl/smf.h engine/src/smf.cpp \
  engine/include/cl/engine.h engine/src/engine.cpp engine/src/engine_impl.h engine/src/pad_programs.cpp \
  tests/engine/SlicerTests.cpp tests/engine/SmfTests.cpp
```

Expected: no output.

```bash
git add -A engine tools tests
git commit -m "feat(cutline)!: port the MIDI slicer and export to literal steps; delete the 2.0 generator"
```

---
### Task 5: UI port

A minimal port: enough for the plugin to stay usable. The grid spec does the real interaction design, so do not add controls beyond the ones listed here.

The grid's logic (what a cell looks like, what an edit does) is new code, given in full and tested without a window. The panels are ports of existing files: where a step gives a change list instead of a full listing, the existing function is the starting point and everything the list does not mention stays as it is.

This task builds only the UI library, its tests and the snapshot tool. The plugin does not build until Task 6.

**Files:**
- Rewrite: `ui/include/cl/ui/model.h`, `ui/include/cl/ui/fake_model.h`, `ui/src/fake_model.cpp`
- Create: `ui/src/grid_edit.h`, `ui/src/grid_edit.cpp`, `tests/ui/GridEditTests.cpp`
- Rewrite: `ui/src/grid_stage.h`, `ui/src/grid_stage_draw.cpp`, `ui/src/grid_stage_input.cpp`
- Modify: `ui/src/pulse_ribbon.cpp`, `ui/src/setup_panel.cpp`, `ui/src/play_panel.h`, `ui/src/play_panel.cpp`, `ui/src/pad_art.h`, `ui/src/pad_art.cpp`, `ui/src/theme.h`, `ui/src/editor.cpp`, `ui/tools/snapshot.cpp`
- Modify: `ui/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `GrooveState`, `PlayPosition`, `SlicerPlan`, `HeldNotes` (`cl/engine.h`); `resolveBar` with a cached `DensityMap` (`cl/resolve.h`); `sanitise`, `setPatternBars`, `patternSteps`, `laneLoopSteps`, `fillWindowStart` (`cl/groove.h`); `effectiveLaneMidiNote`, `kPerfPad*` tables (`cl/pattern.h`).
- Produces, in `cl/ui/model.h`:

```cpp
struct Visuals {
    PlayPosition position{};   // absolute bar, bar in pattern, step in bar, fill live, Repeat window
    SlicerPlan slicerPlan{};   // the slicer keeps its plan snapshot
    HeldNotes heldNotes{};
    int activeScene = 0;
    int pendingScene = -1;
    int activePadProgram = 0;
    int pendingPadProgram = -1;
    bool padEngaged = false;
    std::uint8_t midiPadMask = 0;
    int capturedClipCount = 0;
    double bpm = 172.0;
    double ppq = 0.0;
    std::array<float, kLaneCount> lanePhase{};
    std::array<float, kLaneCount> laneValue{};
    std::array<float, kDrumLaneCount> laneFlash{};
    std::uint64_t activeNotesLow = 0;
    std::uint64_t activeNotesHigh = 0;
    int lastBlockEvents = 0;
    std::uint8_t activityMask = 0;
    bool playing = false;
};
```

  and on `Model`: `virtual const GrooveState& groove() const = 0;` (the bank and the density maps the engine plays from) and `virtual void setGrooveBank(const GrooveBank& bank) = 0;` in place of `patternBank()` and `setPatternBank()`. `mutateActiveScene()` and `polymeterActiveScene()` are removed. `cloneActiveSceneToNext()` and `clearActiveScene()` stay. Removed from `Visuals`: `plan`, `phraseIndex`, `playheadBeats`.
- Produces, in `ui/src/grid_edit.h` (namespace `cl::ui::grid`): `CellLook`, `Cell`, `cellOf`, `fillCellOf`, `heightFraction`, `Geometry`, `cellAt`, `View`, `resolveView`, `resolveFillView`, `stepAt`, `toggleStep`, `setStepVelocity`, `cycleRatchet`, `setStepLayer`, `setFillBeats`, `setLaneLength`. Signatures are in Step 3.

What the port shows and does, from the spec:

- The plugin opens on GRID.
- The grid shows every bar of the pattern: 16, 32 or 64 columns. It draws `resolveBar` output and nothing else: Core, Groove and Detail at three heights; a step that is written but silent (Density, mute, another lane's solo) as an outline; fill steps in amber while the fill is live.
- The playhead is the absolute step from telemetry: `position.barInPattern * 16 + position.stepInBar`.
- Click toggles a step (new steps: Groove, velocity 96). Vertical drag on a lit step sets its velocity; a click without a drag switches it off. Shift-click cycles ratchet. Right-click opens the step menu, which sets the layer.
- A Fill toggle switches the grid to the 16-column fill bar. Only the fill window can be edited there.
- The header has Clone, Clear, Fill, and `1 | 2 | 4` pills: bars in the normal view, fill beats in the fill view. Mutate, Polymeter and lane Lock are gone.
- Macro row: Density, Swing, Dynamics, Fill.
- PLAY shows the four interim pad programs with their existing art.
- The pulse ribbon draws from `resolveBar`.

- [x] **Step 1: Write the failing grid-logic tests**

Create `tests/ui/GridEditTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// The grid's logic without a window: what each cell looks like, and what each edit does.
#include "grid_edit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cl;
using namespace cl::ui;
using Catch::Approx;

namespace {

Step hit(int velocity, Layer layer) {
    Step s;
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = layer;
    return s;
}

// Scene B: two bars, one step in each layer on the hat lane, and a one-beat snare fill.
GrooveState layered() {
    GrooveBank bank;
    Pattern& p = bank.scenes[1];
    p.bars = 2;
    p.lanes[0].steps[0] = hit(118, Layer::core);
    p.lanes[4].steps[2] = hit(96, Layer::groove);
    p.lanes[4].steps[3] = hit(56, Layer::detail);
    p.lanes[4].steps[18] = hit(96, Layer::core);
    p.lanes[4].steps[18].ratchet = 3;
    p.lanes[4].steps[18].flam = true;
    p.fill.beats = 1;
    p.fill.steps[1][14] = hit(118, Layer::groove);
    return GrooveState(bank);
}

} // namespace

TEST_CASE("The view resolves every bar of the scene with the engine's maps", "[grid]") {
    const GrooveState groove = layered();
    const grid::View view = grid::resolveView(groove, 1, 0.5f, 0.5f, -1);
    REQUIRE(view.bars == 2);
    CHECK(grid::cellOf(view.bar[0].lanes[0][0]).look == grid::CellLook::core);
    CHECK(grid::cellOf(view.bar[0].lanes[0][0]).velocity == 118);
    CHECK(grid::cellOf(view.bar[0].lanes[4][2]).look == grid::CellLook::groove);
    CHECK(grid::cellOf(view.bar[0].lanes[4][3]).look == grid::CellLook::outline); // Detail is silent at 50%
    CHECK(grid::cellOf(view.bar[0].lanes[4][5]).look == grid::CellLook::empty);
    const grid::Cell ratcheted = grid::cellOf(view.bar[1].lanes[4][2]);
    CHECK(ratcheted.look == grid::CellLook::core);
    CHECK(ratcheted.ratchet == 3);
    CHECK(ratcheted.flam);

    const grid::View full = grid::resolveView(groove, 1, 1.0f, 0.5f, -1);
    CHECK(grid::cellOf(full.bar[0].lanes[4][3]).look == grid::CellLook::detail);

    const grid::View defaults = grid::resolveView(GrooveState(), 0, 0.5f, 0.5f, -1);
    CHECK(defaults.bars == 1);
    CHECK(grid::cellOf(defaults.bar[0].lanes[1][4]).look == grid::CellLook::core);
}

TEST_CASE("A live fill shows its steps in the fill look, in its own bar only", "[grid]") {
    const GrooveState groove = layered();
    const grid::View idle = grid::resolveView(groove, 1, 0.5f, 0.5f, -1);
    CHECK(grid::cellOf(idle.bar[1].lanes[1][14]).look == grid::CellLook::empty);

    const grid::View live = grid::resolveView(groove, 1, 0.5f, 0.5f, 1);
    CHECK(grid::cellOf(live.bar[1].lanes[1][14]).look == grid::CellLook::fill);
    CHECK(grid::cellOf(live.bar[0].lanes[1][14]).look == grid::CellLook::empty);

    const ResolvedBar fillBar = grid::resolveFillView(groove, 1, 0.5f, 0.5f);
    CHECK(grid::cellOf(fillBar.lanes[1][14]).look == grid::CellLook::fill);
    CHECK(grid::cellOf(fillBar.lanes[4][2]).look == grid::CellLook::core); // the last bar's own hat shows through

    // The fill editor lights only fill steps. A main-pattern step that shows through is an outline.
    CHECK(grid::fillCellOf(fillBar.lanes[1][14]).look == grid::CellLook::fill);
    CHECK(grid::fillCellOf(fillBar.lanes[1][14]).velocity == 118);
    CHECK(grid::fillCellOf(fillBar.lanes[4][2]).look == grid::CellLook::outline);
    CHECK(grid::fillCellOf(fillBar.lanes[4][2]).velocity == 0);
    CHECK(grid::fillCellOf(fillBar.lanes[4][5]).look == grid::CellLook::empty);
}

TEST_CASE("A muted lane's steps are outlines, and the three layers have three heights", "[grid]") {
    GrooveBank bank;
    bank.scenes[0].lanes[4].muted = true;
    const grid::View view = grid::resolveView(GrooveState(bank), 0, 0.5f, 0.5f, -1);
    CHECK(grid::cellOf(view.bar[0].lanes[4][0]).look == grid::CellLook::outline);
    CHECK(grid::cellOf(view.bar[0].lanes[0][0]).look == grid::CellLook::core);

    CHECK(grid::heightFraction(Layer::core) == Approx(1.0f));
    CHECK(grid::heightFraction(Layer::groove) < grid::heightFraction(Layer::core));
    CHECK(grid::heightFraction(Layer::detail) < grid::heightFraction(Layer::groove));
}

TEST_CASE("A point maps to its lane and step at 16, 32 and 64 columns", "[grid]") {
    for (const int steps : {16, 32, 64}) {
        const grid::Geometry g{100.0f, 40.0f, 640.0f / static_cast<float>(steps), 20.0f, steps};
        int lane = -1;
        int step = -1;
        INFO("steps " << steps);
        REQUIRE(grid::cellAt(g, 100.0f, 40.0f, lane, step));
        CHECK(lane == 0);
        CHECK(step == 0);
        REQUIRE(grid::cellAt(g, 100.0f + 639.0f, 40.0f + 159.0f, lane, step));
        CHECK(lane == 7);
        CHECK(step == steps - 1);
        REQUIRE(grid::cellAt(g, 100.0f + 320.5f, 40.0f + 61.0f, lane, step)); // just past the middle, fourth row
        CHECK(lane == 3);
        CHECK(step == steps / 2);
        CHECK_FALSE(grid::cellAt(g, 99.0f, 50.0f, lane, step));   // left of the grid: the sidebar
        CHECK_FALSE(grid::cellAt(g, 741.0f, 50.0f, lane, step));  // right of the last column
        CHECK_FALSE(grid::cellAt(g, 200.0f, 39.0f, lane, step));  // above the first lane
        CHECK_FALSE(grid::cellAt(g, 200.0f, 201.0f, lane, step)); // below the last lane
    }
}

TEST_CASE("Toggling a step writes a Groove step at velocity 96, or clears it", "[grid]") {
    Pattern p = defaultBeat();
    CHECK(grid::toggleStep(p, false, 2, 5));
    CHECK(p.lanes[2].steps[5].velocity == 96);
    CHECK(p.lanes[2].steps[5].layer == Layer::groove);
    CHECK(grid::toggleStep(p, false, 2, 5));
    CHECK(p.lanes[2].steps[5] == Step{});
    CHECK(grid::toggleStep(p, false, 0, 0)); // a Core step toggles off like any other
    CHECK_FALSE(p.lanes[0].steps[0].on());

    CHECK_FALSE(grid::toggleStep(p, false, 0, 16)); // beyond a one-bar pattern
    CHECK_FALSE(grid::toggleStep(p, false, 8, 0));
    CHECK(grid::stepAt(p, false, 0, 16) == nullptr);
}

TEST_CASE("The fill view edits only the fill window", "[grid]") {
    Pattern p = defaultBeat();
    p.fill.beats = 2;
    CHECK_FALSE(grid::toggleStep(p, true, 1, 4)); // before the window
    CHECK(grid::stepAt(p, true, 1, 4) == nullptr);
    CHECK(grid::toggleStep(p, true, 1, 12));
    CHECK(p.fill.steps[1][12].velocity == 96);
    CHECK(p.lanes[1].steps[12].on()); // the main pattern is untouched

    CHECK(grid::setFillBeats(p, 1));
    CHECK(p.fill.beats == 1);
    CHECK(grid::toggleStep(p, true, 1, 9) == false); // now outside the one-beat window
    p.fill.steps[1][13].velocity = 96;
    CHECK(grid::setFillBeats(p, 4));
    CHECK(p.fill.steps[1][13].on());
    CHECK_FALSE(grid::setFillBeats(p, 4)); // no change
    CHECK_FALSE(grid::setFillBeats(p, 3)); // only 1, 2 or 4
}

TEST_CASE("Velocity, ratchet and layer edits apply to lit steps only", "[grid]") {
    Step off;
    CHECK_FALSE(grid::setStepVelocity(off, 100));
    CHECK_FALSE(grid::cycleRatchet(off));
    CHECK_FALSE(grid::setStepLayer(off, Layer::core));
    CHECK(off == Step{});

    Step s = hit(96, Layer::groove);
    CHECK(grid::setStepVelocity(s, 300));
    CHECK(s.velocity == 127);
    CHECK(grid::setStepVelocity(s, -5));
    CHECK(s.velocity == 1); // dragging down never switches a step off
    CHECK_FALSE(grid::setStepVelocity(s, 1));

    CHECK(grid::cycleRatchet(s));
    CHECK(s.ratchet == 2);
    grid::cycleRatchet(s);
    grid::cycleRatchet(s);
    CHECK(s.ratchet == 4);
    grid::cycleRatchet(s);
    CHECK(s.ratchet == 1);

    CHECK(grid::setStepLayer(s, Layer::core));
    CHECK(s.layer == Layer::core);
    CHECK_FALSE(grid::setStepLayer(s, Layer::core));
}

TEST_CASE("A lane length equal to the pattern means follow the pattern", "[grid]") {
    Pattern p = defaultBeat();
    CHECK(grid::setLaneLength(p, 6, 7));
    CHECK(p.lanes[6].length == 7);
    CHECK(grid::setLaneLength(p, 6, 16));
    CHECK(p.lanes[6].length == 0);
    CHECK(grid::setLaneLength(p, 6, 0) == false);
    CHECK(grid::setLaneLength(p, 6, 99) == false); // clamps to the pattern: still follow
    CHECK(p.lanes[6].length == 0);
}
```

In `tests/CMakeLists.txt`, change the `cl-ui-tests` line to:

```cmake
    add_executable(cl-ui-tests ui/CurveEditTests.cpp ui/GridEditTests.cpp)
```

In `ui/CMakeLists.txt`, add `src/grid_edit.cpp` and `src/grid_edit.h` to the `cl_ui` source list, before `src/grid_stage.h`.

- [x] **Step 2: Run them to verify they fail**

```bash
cmake --build --preset dev --target cl-ui-tests
```

Expected: the build fails. The first errors come from `ui/include/cl/ui/model.h` (`cl/phrase.h` no longer has `PhrasePlan`) or from the missing `grid_edit.h`.

- [x] **Step 3: Add the grid logic**

Create `ui/src/grid_edit.h`:

```cpp
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

// Where the step cells are, in panel pixels: `steps` columns of `stepW` from `gridX`, eight rows of `rowH` from `gridY`.
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
bool setStepVelocity(Step& step, int velocity) noexcept;                        // lit steps only, clamped to 1..127
bool cycleRatchet(Step& step) noexcept;                                         // lit steps only: 1, 2, 3, 4, 1
bool setStepLayer(Step& step, Layer layer) noexcept;                            // lit steps only
bool setFillBeats(Pattern& pattern, int beats) noexcept;                        // 1, 2 or 4
bool setLaneLength(Pattern& pattern, int lane, int steps) noexcept;             // 1..pattern steps; the full length means follow

} // namespace cl::ui::grid
```

Create `ui/src/grid_edit.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "grid_edit.h"

#include <algorithm>
#include <cstddef>

namespace cl::ui::grid {

Cell cellOf(const ResolvedStep& step) noexcept {
    Cell cell;
    if (!step.written)
        return cell;
    cell.layer = step.layer;
    cell.ratchet = step.ratchet;
    cell.flam = step.flam;
    if (step.velocity == 0) {
        cell.look = CellLook::outline;
        return cell;
    }
    cell.velocity = step.velocity;
    if (step.fromFill)
        cell.look = CellLook::fill;
    else
        cell.look = step.layer == Layer::core ? CellLook::core
                                              : (step.layer == Layer::detail ? CellLook::detail : CellLook::groove);
    return cell;
}

Cell fillCellOf(const ResolvedStep& step) noexcept {
    Cell cell = cellOf(step);
    if (cell.look != CellLook::empty && cell.look != CellLook::fill) {
        cell.look = CellLook::outline;
        cell.velocity = 0;
    }
    return cell;
}

float heightFraction(Layer layer) noexcept {
    return layer == Layer::core ? 1.0f : (layer == Layer::detail ? 0.44f : 0.72f);
}

View resolveView(const GrooveState& groove, int scene, float density, float dynamics, int liveFillBar) noexcept {
    const auto idx = static_cast<std::size_t>(std::clamp(scene, 0, kSceneCount - 1));
    const Pattern& pattern = groove.bank.scenes[idx];
    View view;
    view.bars = patternBars(pattern);
    for (int bar = 0; bar < view.bars; ++bar)
        resolveBar(pattern, groove.maps[idx], bar, {density, dynamics, bar == liveFillBar},
                   view.bar[static_cast<std::size_t>(bar)]);
    return view;
}

ResolvedBar resolveFillView(const GrooveState& groove, int scene, float density, float dynamics) noexcept {
    const auto idx = static_cast<std::size_t>(std::clamp(scene, 0, kSceneCount - 1));
    const Pattern& pattern = groove.bank.scenes[idx];
    ResolvedBar bar;
    resolveBar(pattern, groove.maps[idx], patternBars(pattern) - 1, {density, dynamics, true}, bar);
    return bar;
}

bool cellAt(const Geometry& geometry, float x, float y, int& lane, int& step) noexcept {
    if (geometry.stepW <= 0.0f || geometry.rowH <= 0.0f || x < geometry.gridX || y < geometry.gridY)
        return false;
    const int column = static_cast<int>((x - geometry.gridX) / geometry.stepW);
    const int row = static_cast<int>((y - geometry.gridY) / geometry.rowH);
    if (column >= geometry.steps || row >= kDrumLaneCount)
        return false;
    lane = row;
    step = column;
    return true;
}

Step* stepAt(Pattern& pattern, bool fillView, int lane, int step) noexcept {
    if (lane < 0 || lane >= kDrumLaneCount || step < 0)
        return nullptr;
    const auto li = static_cast<std::size_t>(lane);
    if (fillView) {
        if (step >= kStepsPerBar || step < fillWindowStart(pattern.fill))
            return nullptr;
        return &pattern.fill.steps[li][static_cast<std::size_t>(step)];
    }
    if (step >= patternSteps(pattern))
        return nullptr;
    return &pattern.lanes[li].steps[static_cast<std::size_t>(step)];
}

bool toggleStep(Pattern& pattern, bool fillView, int lane, int step) noexcept {
    Step* s = stepAt(pattern, fillView, lane, step);
    if (s == nullptr)
        return false;
    if (s->on()) {
        *s = {};
    } else {
        *s = {};
        s->velocity = 96;
        s->layer = Layer::groove;
    }
    return true;
}

bool setStepVelocity(Step& step, int velocity) noexcept {
    if (!step.on())
        return false;
    const auto next = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
    if (next == step.velocity)
        return false;
    step.velocity = next;
    return true;
}

bool cycleRatchet(Step& step) noexcept {
    if (!step.on())
        return false;
    step.ratchet = static_cast<std::uint8_t>(step.ratchet >= 4 ? 1 : step.ratchet + 1);
    return true;
}

bool setStepLayer(Step& step, Layer layer) noexcept {
    if (!step.on() || step.layer == layer)
        return false;
    step.layer = layer;
    return true;
}

bool setFillBeats(Pattern& pattern, int beats) noexcept {
    if ((beats != 1 && beats != 2 && beats != 4) || static_cast<int>(pattern.fill.beats) == beats)
        return false;
    pattern.fill.beats = static_cast<std::uint8_t>(beats);
    sanitise(pattern); // steps before the new window are no longer part of the fill
    return true;
}

bool setLaneLength(Pattern& pattern, int lane, int steps) noexcept {
    if (lane < 0 || lane >= kDrumLaneCount || steps < 1)
        return false;
    const int total = patternSteps(pattern);
    const int clamped = std::clamp(steps, 1, total);
    const auto next = static_cast<std::uint8_t>(clamped >= total ? 0 : clamped);
    std::uint8_t& length = pattern.lanes[static_cast<std::size_t>(lane)].length;
    if (length == next)
        return false;
    length = next;
    return true;
}

} // namespace cl::ui::grid
```

- [x] **Step 4: Rewrite the model interface and the fake model**

Replace `ui/include/cl/ui/model.h`: keep `PresetInfo` and every `Model` member not named below; replace the includes and `Visuals` with the versions in this task's Interfaces block; replace the four pattern members with

```cpp
    // The bank and the density maps the engine plays from. The grid and ribbon resolve with these.
    virtual const GrooveState& groove() const = 0;
    virtual void setGrooveBank(const GrooveBank& bank) = 0;

    virtual void cloneActiveSceneToNext() = 0;
    virtual void clearActiveScene() = 0;
```

The includes become `"cl/engine.h"`, `"cl/lanes.h"`, `"cl/params.h"`, `"cl/pattern.h"`, `<array>`, `<cstdint>`, `<string>`, `<vector>` (`cl/engine.h` brings `GrooveState`, `PlayPosition`, `HeldNotes` and `SlicerPlan`).

In `ui/include/cl/ui/fake_model.h`: replace `PatternBank patternBank()` and `setPatternBank` with

```cpp
    const GrooveState& groove() const override { return groove_; }
    void setGrooveBank(const GrooveBank& bank) override {
        groove_ = GrooveState(bank);
        refresh();
    }

    // For screenshots: what the snapshot tool can ask the fake model to show.
    void showBars(int bars); // resize scene A to 1, 2 or 4 bars
    void showLive();         // the last bar with its fill live, and a one-beat Repeat window held
    void showSlicer();       // MIDI Slicer mode
```

add the private member `bool live_ = false;`,

remove `mutateActiveScene`, `polymeterActiveScene` and `actionSeed_`; rename `recompile()` to `refresh()`; replace the `bank_` member with `GrooveState groove_{};`; change `exportStatus()` to return `"Drag 2-bar .mid"`; change the preset list and `presetName_` to `{"Amen", "Breaks"}, {"Neuro", "Neuro"}, {"Slicer: UKG Chord Stabs", "MIDI Slicer"}` and `"Amen"`.

Replace `ui/src/fake_model.cpp` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/fake_model.h"

#include "cl/phrase.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cl::ui {

namespace {

void put(Pattern& p, int lane, int step, int velocity, Layer layer) {
    Step& s = p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = layer;
}

// A two-bar break for screenshots: every layer, a ratchet, a flam, a short lane and a drawn fill.
Pattern showcasePattern() {
    Pattern p{};
    p.bars = 2;
    for (const int step : {0, 10, 16, 26})
        put(p, 0, step, 118, Layer::core);
    put(p, 0, 2, 96, Layer::groove);
    put(p, 0, 19, 96, Layer::detail);
    for (const int step : {4, 12, 20, 28})
        put(p, 1, step, 118, Layer::core);
    put(p, 1, 27, 96, Layer::groove);
    for (const int step : {7, 9, 23, 25})
        put(p, 2, step, 56, Layer::detail);
    put(p, 3, 12, 96, Layer::groove);
    for (int step = 0; step < 32; step += 2)
        put(p, 4, step, step % 4 == 0 ? 118 : 96, Layer::core);
    for (int step = 1; step < 32; step += 4)
        put(p, 4, step, 56, Layer::detail);
    p.lanes[4].steps[14].ratchet = 2;
    put(p, 5, 14, 96, Layer::groove);
    put(p, 5, 30, 96, Layer::groove);
    for (const int step : {0, 3, 6})
        put(p, 6, step, 96, Layer::groove);
    p.lanes[6].length = 7;
    put(p, 7, 0, 96, Layer::core);
    p.lanes[1].steps[12].flam = true;
    p.fill.beats = 2;
    for (const int step : {8, 10, 12, 14})
        p.fill.steps[1][static_cast<std::size_t>(step)].velocity = 96;
    p.fill.steps[1][15].velocity = 118;
    p.fill.steps[0][8].velocity = 96;
    return p;
}

} // namespace

FakeModel::FakeModel() {
    params_[ParamId::density] = 0.6f;
    params_[ParamId::swing] = 0.18f;
    params_[ParamId::fillEvery] = 2.0f;
    GrooveBank bank;
    bank.scenes[0] = showcasePattern();
    groove_ = GrooveState(bank);
    refresh();
}

void FakeModel::set(ParamId id, float real) {
    params_[id] = clampToRange(spec(id), real);
    refresh();
}

void FakeModel::cloneActiveSceneToNext() {
    const int cur = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    const int next = (cur + 1) % kSceneCount;
    GrooveBank bank = groove_.bank;
    bank.scenes[static_cast<std::size_t>(next)] = bank.scenes[static_cast<std::size_t>(cur)];
    groove_ = GrooveState(bank);
    set(ParamId::scene, static_cast<float>(next));
}

void FakeModel::clearActiveScene() {
    const int scene = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    GrooveBank bank = groove_.bank;
    bank.scenes[static_cast<std::size_t>(scene)] = Pattern{};
    groove_ = GrooveState(bank);
    refresh();
}

void FakeModel::showBars(int bars) {
    GrooveBank bank = groove_.bank;
    setPatternBars(bank.scenes[0], bars);
    groove_ = GrooveState(bank);
    refresh();
}

void FakeModel::showLive() {
    live_ = true;
    refresh();
}

void FakeModel::showSlicer() { set(ParamId::engineMode, static_cast<float>(EngineMode::slicer)); }

void FakeModel::refresh() {
    const int scene = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    visuals_.bpm = 172.0;
    visuals_.activeScene = scene;
    visuals_.pendingScene = -1;
    visuals_.activePadProgram = std::clamp(params_.choice(ParamId::padProgram), 0, kPerfPadCount - 1);
    visuals_.pendingPadProgram = -1;
    visuals_.padEngaged = params_.on(ParamId::padTouch) || params_.on(ParamId::padHold);
    visuals_.midiPadMask = 0;
    // Bar 1 of the pattern, on the snare backbeat.
    visuals_.position = {};
    visuals_.position.absBar = 0;
    visuals_.position.barInPattern = 0;
    visuals_.position.stepInBar = 12;
    if (live_) {
        // The last bar, inside its fill window, with Repeat holding beat 2 of that bar.
        const int bars = patternBars(groove_.bank.scenes[static_cast<std::size_t>(scene)]);
        visuals_.position.absBar = bars - 1;
        visuals_.position.barInPattern = bars - 1;
        visuals_.position.stepInBar = 13;
        visuals_.position.fillLive = true;
        visuals_.position.repeatStartStep = (bars - 1) * kStepsPerBar + 4;
        visuals_.position.repeatBeats = 1.0;
        visuals_.padEngaged = true;
    }
    visuals_.ppq = 3.0;
    visuals_.playing = true;
    visuals_.lastBlockEvents = 6;
    visuals_.activityMask = static_cast<std::uint8_t>(kActNote | (visuals_.padEngaged ? kActPad : 0));
    visuals_.laneFlash = {0.35f, 0.96f, 0.0f, 0.0f, 0.68f, 0.0f, 0.0f, 0.0f};
    visuals_.heldNotes.clear();
    for (const int note : {48, 55, 60})
        visuals_.heldNotes.push(note, 100);
    compileSlicer(params_, groove_, scene, visuals_.heldNotes, visuals_.bpm, visuals_.slicerPlan);
    for (int l = 0; l < kLaneCount; ++l) {
        const auto idx = static_cast<std::size_t>(l);
        visuals_.lanePhase[idx] = static_cast<float>(
            lanePhase(visuals_.ppq, laneLengthQuarters(params_, l), params_[laneParam(l, LaneField::phase)]));
        visuals_.laneValue[idx] = static_cast<float>(laneSend(params_, curves_, l, visuals_.ppq));
    }
}

const Visuals& FakeModel::visuals() { return visuals_; }

} // namespace cl::ui
```

- [x] **Step 5: Run the grid-logic tests**

`cl-ui-tests` links `cl_ui`, and `cl_ui` does not compile until every panel is ported. Go straight on to Steps 6 to 10; Step 11 runs:

```bash
cmake --build --preset dev --target cl-ui-tests && build/dev/tests/cl-ui-tests
```

Expected (after Step 10): `All tests passed`.

- [x] **Step 6: Port the theme, the pad art and the macro row**

`ui/src/theme.h`: change the header comment to "Cutline 3 dark-chrome design tokens, the 8-lane palette and the four pad colours." Replace the `pad` array with

```cpp
inline constexpr std::array<unsigned, kPerfPadCount> pad{
    coral,     // 0: Repeat
    vermilion, // 1: Roll
    teal,      // 2: Rate
    dropRed,   // 3: Drop
};
```

and remove the "0: SLICE", "3: SWING" style notes from the colour comments (the colours themselves stay; lanes use them).

`ui/src/pad_art.cpp` and `pad_art.h`: the art is unchanged, only its numbering moves. `drawStutter` is Repeat's art, `drawRoll` is Roll's, `drawMorph` is Rate's, `drawDrop` is Drop's. Delete `drawSlice`, `drawBounce`, `drawSwing` and `drawPoly`, and any helper that only they used (`-Werror` reports unused functions). Replace the `switch` in `drawCutlinePadArt` with

```cpp
    switch (std::clamp(program, 0, kPerfPadCount - 1)) {
    case 0:
        drawStutter(canvas, x, y, w, h, card, m, scale); // Repeat
        break;
    case 1:
        drawRoll(canvas, x, y, w, h, card, m, scale);
        break;
    case 2:
        drawMorph(canvas, x, y, w, h, card, m, scale); // Rate
        break;
    default:
        drawDrop(canvas, x, y, w, h, card, m, scale);
        break;
    }
```

Remove `laneLens` from `PadMotion` if no remaining art function reads it. Update both files' header comments from "8 playable XY articulation worlds" to "the four interim pad programs".

`ui/src/setup_panel.cpp`: replace `kMacros` with

```cpp
constexpr std::array<MacroDef, 4> kMacros{{
    {ParamId::density, colour::iris},
    {ParamId::swing, colour::vermilion},
    {ParamId::dynamics, colour::teal},
    {ParamId::fillEvery, colour::emerald},
}};
```

and in `SetupPanel` replace the `bars_`, `pocket_`, `fillType_` and `variation_` rows with one row, `quant_(context, ParamId::sceneQuantize, colour::iris)`. The panel's rows are then, top to bottom: `noteMap_` or `slicerStyle_` (as today, by mode), `quant_`, `strum_`, `gate_`. Update the constructor's child list, `resized()` and the member list to match.

- [x] **Step 7: Rewrite the grid stage**

`ui/src/grid_stage.h`: include `"grid_edit.h"`. In the class:

- `DragMode` becomes `{ none, paintOn, velocity, laneLength, slicerLength, laneNote }`.
- `Layout`: remove `modeDrum`, `modeSlicer`, `scenes`, `btnMutate`, `btnPoly`. Keep `btnClone`, `btnClear`. Add `Rect btnFill;`, `std::array<Rect, 3> lengthPills{};`, `int steps = 16;` (columns in the drum grid) and `float slicerStepW = 0.0f;`.
- Remove `laneLockRect`. `laneEndHandleX(int laneLen, const Layout&)` stays for the drum lanes. Add `float slicerEndHandleX(int length, const Layout&) const;`, which returns `lay.gridX + clamp(length, 1, kMaxSlicerSteps) * lay.slicerStepW`: the gate strip always has 32 columns, whatever the pattern's length, so it must not use the drum grid's `stepW`. Every slicer hover, drag and draw of the length handle uses it.
- `computeLayout` takes the column count: `Layout computeLayout(float w, float h, int steps) const;`.
- Add `int gridSteps() const;` (16 in the fill view, otherwise `patternSteps` of the EDITED scene, `editScene()`: layout, drawing and edits all follow the scene the `scene` parameter selects, and only the playhead, live-fill and Repeat overlays wait for the engine to report that scene as active), `int editScene() const;` (the `scene` parameter, clamped), and `void edit(const std::function<bool(GrooveBank&)>& change);`, which copies `context_.model.groove().bank`, applies `change`, and calls `context_.model.setGrooveBank(bank)` only if `change` returned true.
- Members: remove `hoverScene_`; `hoverAction_` is now `0 = Clone, 1 = Clear, 2 = Fill, 10..12 = length pills`; replace `dragBaseVelOff_` with `int dragBaseVel_ = 96;`; add the accessor `bool fillView() const { return context_.fillView; }`. The flag itself lives in the shared UI `Context` (`ui/src/theme.h`: add `bool fillView = false;`), so the editor and the snapshot tool can switch the view.

`ui/src/grid_stage_draw.cpp`. Keep `drawPill`, the activity LED code and `tick()`. Change the rest as follows.

`computeLayout`: the header row holds, left to right from `pad + px(222)`: Clone (68 wide), Clear (54), Fill (48), then three pills of 26 each for `1`, `2`, `4`, each separated by `px(6)`. `lay.steps = steps`, `lay.stepW = lay.gridW / steps`, `lay.slicerStepW = lay.gridW / kMaxSlicerSteps`. The slicer rectangles are as before.

`drawHeader`: the title is `"PATTERN"`, `"FILL"` when `fillView()`, or `"MIDI SLICER"` in slicer mode. Draw Clone and Clear as today. Draw Fill lit (amber, filled) when `fillView()`. Draw the three pills; the lit one is the pattern's bars, or the fill's beats when `fillView()`; put a small label `"bars"` or `"beats"` after them. In slicer mode draw Clone and Clear only (no Fill, no pills): the spec keeps both. The right-hand readout is `"%.0f BPM"` plus the Rate choice when it is not 1x; it no longer names a style.

`drawDrumGrid`, in full:

```cpp
void GridStagePanel::drawDrumGrid(visage::Canvas& canvas, const Layout& lay, const Visuals& vis) {
    const GrooveState& groove = context_.model.groove();
    // The grid always shows the scene that edits go to. While a quantized scene switch is pending, the
    // engine is still playing the previous scene, so the playhead, live fill and Repeat window are hidden
    // until the engine reports this scene as the active one.
    const int scene = editScene();
    const bool live = vis.playing && vis.activeScene == scene;
    const Pattern& pattern = groove.bank.scenes[static_cast<std::size_t>(scene)];
    const float density = context_.model.get(ParamId::density);
    const float dynamics = context_.model.get(ParamId::dynamics);
    const auto noteMap = static_cast<NoteMap>(
        std::clamp(static_cast<int>(context_.model.get(ParamId::noteMap) + 0.5f), 0, kNoteMapCount - 1));
    const bool anySolo = std::any_of(pattern.lanes.begin(), pattern.lanes.end(), [](const DrumLane& l) { return l.solo; });

    // What sounds, from the same function and the same maps as the engine.
    const int liveFillBar = (live && vis.position.fillLive) ? vis.position.barInPattern : -1;
    const grid::View view = grid::resolveView(groove, scene, density, dynamics, liveFillBar);
    const ResolvedBar fillBar = grid::resolveFillView(groove, scene, density, dynamics);
    const int fillFrom = fillWindowStart(pattern.fill);
    const int steps = lay.steps;

    // Ruler.
    for (int st = 0; st < steps; st += 4) {
        const float sx = lay.gridX + static_cast<float>(st) * lay.stepW;
        const int bar = st / kStepsPerBar + 1;
        const int beat = (st % kStepsPerBar) / 4 + 1;
        char label[16]{};
        if (beat == 1 && fillView())
            std::snprintf(label, sizeof(label), "FILL");
        else if (beat == 1)
            std::snprintf(label, sizeof(label), "BAR %d", bar);
        else
            std::snprintf(label, sizeof(label), "%d.%d", bar, beat);
        if (steps <= 32 || beat == 1)
            drawLabel(canvas, context_, label, sx + px(2.0f), lay.rulerY, lay.stepW * 3.5f, px(13.0f),
                      beat == 1 ? colour::text : colour::textFaint, visage::Font::kLeft, 9.0f,
                      beat == 1 ? Weight::semibold : Weight::medium);
    }

    // Beat shading and bar lines.
    for (int st = 0; st < steps; ++st) {
        const float sx = lay.gridX + static_cast<float>(st) * lay.stepW;
        if (((st / 4) & 1) == 1) {
            canvas.setColor(0x09ffffff);
            canvas.rectangle(sx, lay.gridY, lay.stepW, lay.gridH);
        }
        if ((st % kStepsPerBar) == 0) {
            canvas.setColor(colour::withAlpha(colour::iris, 0.34f));
            canvas.rectangle(sx, lay.gridY, px(1.5f), lay.gridH);
        } else if ((st % 4) == 0) {
            canvas.setColor(0x20ffffff);
            canvas.rectangle(sx, lay.gridY, px(1.0f), lay.gridH);
        }
    }

    // The fill window: always in the fill view, and over the live bar while a fill plays.
    const int shadeBar = fillView() ? 0 : liveFillBar;
    if (shadeBar >= 0) {
        const float fx = lay.gridX + static_cast<float>(shadeBar * kStepsPerBar + fillFrom) * lay.stepW;
        const float fw = static_cast<float>(kStepsPerBar - fillFrom) * lay.stepW;
        canvas.setColor(colour::withAlpha(colour::amber, 0.07f));
        canvas.rectangle(fx, lay.gridY, fw, lay.gridH);
        canvas.setColor(colour::withAlpha(colour::amber, 0.45f));
        canvas.rectangle(fx, lay.gridY, px(1.2f), lay.gridH);
    }

    // The Repeat window, while the pad holds it.
    if (!fillView() && live && vis.position.repeatStartStep >= 0) {
        const float rx = lay.gridX + static_cast<float>(vis.position.repeatStartStep) * lay.stepW;
        const float rw = static_cast<float>(vis.position.repeatBeats * 4.0) * lay.stepW;
        canvas.setColor(colour::withAlpha(colour::coral, 0.16f));
        canvas.rectangle(rx, lay.gridY, rw, lay.gridH);
    }

    const int playCol = !live ? -1
                                     : (fillView() ? (vis.position.fillLive ? vis.position.stepInBar : -1)
                                                  : vis.position.barInPattern * kStepsPerBar + vis.position.stepInBar);

    for (int l = 0; l < kDrumLaneCount; ++l) {
        const auto li = static_cast<std::size_t>(l);
        const float ry = lay.gridY + static_cast<float>(l) * lay.rowH;
        const unsigned hue = colour::lane[li];
        const DrumLane& lane = pattern.lanes[li];
        const bool audible = anySolo ? (lane.solo && !lane.muted) : !lane.muted;
        const float flash = vis.laneFlash[li];
        const int loop = laneLoopSteps(pattern, l);

        // Row stripe and separator.
        canvas.setColor((l & 1) ? 0x07ffffff : 0x00000000);
        canvas.rectangle(lay.sidebarX, ry, width() - 2.0f * lay.sidebarX, lay.rowH);
        canvas.setColor(0x16ffffff);
        canvas.rectangle(lay.sidebarX, ry + lay.rowH - px(1.0f), width() - 2.0f * lay.sidebarX, px(1.0f));

        // Sidebar: hit LED, lane name, note badge, mute, solo, loop length.
        canvas.setColor(colour::withAlpha(hue, audible ? (0.30f + 0.70f * flash) : 0.12f));
        canvas.roundedRectangle(lay.sidebarX, ry + px(4.0f), px(5.0f), lay.rowH - px(8.0f), px(2.5f));
        drawLabel(canvas, context_, std::string(kitLaneName(noteMap, l)), lay.sidebarX + px(10.0f), ry, px(58.0f),
                  lay.rowH, audible ? colour::text : colour::textFaint, visage::Font::kLeft, 10.0f, Weight::semibold);

        const Rect nr = laneNoteRect(l, lay);
        const bool customNote = lane.midiNote >= 1;
        drawPill(canvas, context_, nr.x, nr.y, nr.w, nr.h, midiNoteLabel(effectiveLaneMidiNote(noteMap, l, lane.midiNote)),
                 customNote ? colour::withAlpha(hue, 0.22f) : colour::raised, customNote ? hue : colour::edge,
                 customNote ? colour::text : colour::textDim, 9.0f, Weight::semibold);
        const Rect mr = laneMuteRect(l, lay);
        drawPill(canvas, context_, mr.x, mr.y, mr.w, mr.h, "M",
                 lane.muted ? colour::withAlpha(colour::vermilion, 0.36f) : colour::raised,
                 lane.muted ? colour::vermilion : colour::edge, lane.muted ? colour::text : colour::textFaint, 9.0f,
                 Weight::semibold);
        const Rect sr = laneSoloRect(l, lay);
        drawPill(canvas, context_, sr.x, sr.y, sr.w, sr.h, "S",
                 lane.solo ? colour::withAlpha(colour::amber, 0.36f) : colour::raised,
                 lane.solo ? colour::amber : colour::edge, lane.solo ? colour::text : colour::textFaint, 9.0f,
                 Weight::semibold);
        if (!fillView()) {
            char lenBuf[8]{};
            std::snprintf(lenBuf, sizeof(lenBuf), "%d", loop);
            const bool looped = loop != patternSteps(pattern);
            drawLabel(canvas, context_, lenBuf, lay.sidebarX + px(182.0f), ry, px(30.0f), lay.rowH,
                      looped ? colour::cyan : colour::textDim, visage::Font::kRight, 10.0f,
                      looped ? Weight::semibold : Weight::medium);
        }

        // Cells.
        for (int st = 0; st < steps; ++st) {
            const float cx = lay.gridX + static_cast<float>(st) * lay.stepW + px(2.0f);
            const float cw = std::max(px(2.0f), lay.stepW - px(4.0f));
            const float cy = ry + px(4.0f);
            const float ch = lay.rowH - px(8.0f);
            const bool editable = !fillView() || st >= fillFrom;
            const bool isHover = editable && hoverLane_ == l && hoverStep_ == st;
            const bool isPlay = st == playCol;
            const ResolvedStep& resolved =
                fillView() ? fillBar.lanes[li][static_cast<std::size_t>(st)]
                          : view.bar[static_cast<std::size_t>(st / kStepsPerBar)].lanes[li][static_cast<std::size_t>(st % kStepsPerBar)];
            // In the fill view only fill steps are lit. The main pattern's steps (before the window, and
            // inside it on lanes that draw nothing in the fill) are dim outlines: context, not content.
            const grid::Cell cell = fillView() ? grid::fillCellOf(resolved) : grid::cellOf(resolved);
            const float dim = (fillView() && cell.look == grid::CellLook::outline) ? 0.45f : 1.0f;

            canvas.setColor(isHover ? 0x24ffffff : (isPlay ? 0x1affffff : 0x10ffffff));
            canvas.roundedRectangle(cx, cy, cw, ch, px(3.5f));
            if (cell.look == grid::CellLook::empty)
                continue;

            const bool isFill = cell.look == grid::CellLook::fill;
            const float barH = std::max(px(6.0f), ch * (isFill ? 1.0f : grid::heightFraction(cell.layer)));
            const float by = cy + (ch - barH);
            const unsigned tone = isFill ? colour::amber : hue;

            if (cell.look == grid::CellLook::outline) {
                // Written but silent: Density, mute, or another lane's solo.
                canvas.setColor(colour::withAlpha(tone, 0.45f * dim));
                canvas.roundedRectangleBorder(cx, by, cw, barH, px(3.5f), px(1.0f));
            } else {
                const float vNorm = std::clamp(static_cast<float>(cell.velocity) / 127.0f, 0.14f, 1.0f);
                canvas.setColor(colour::withAlpha(tone, (isPlay ? 0.96f : (0.46f + 0.48f * vNorm)) * dim));
                canvas.roundedRectangle(cx, by, cw, barH, px(3.5f));
                if (isPlay) {
                    canvas.setColor(colour::withAlpha(0xffffffff, 0.85f));
                    canvas.roundedRectangle(cx + px(1.5f), by + px(1.0f), cw - px(3.0f), px(2.5f), px(1.2f));
                }
            }
            if (cell.flam) {
                canvas.setColor(colour::withAlpha(0xffffffff, dim));
                canvas.roundedRectangle(cx - px(1.5f), by + px(3.0f), px(2.5f), std::max(px(4.0f), barH - px(6.0f)),
                                        px(1.0f));
            }
            if (cell.ratchet >= 2 && cw >= px(12.0f)) {
                char rBuf[8]{};
                std::snprintf(rBuf, sizeof(rBuf), "x%d", static_cast<int>(cell.ratchet));
                canvas.setColor(0xd80d1018);
                canvas.roundedRectangle(cx + px(1.0f), cy + px(1.0f), cw - px(2.0f), px(12.0f), px(2.5f));
                canvas.setColor(colour::amber);
                canvas.text(rBuf, font(context_, 8.5f, Weight::semibold), visage::Font::kCenter, cx, cy, cw, px(13.0f));
            }
        }

        // Loop-length handle, in the pattern view.
        if (!fillView()) {
            const float hx = laneEndHandleX(loop, lay);
            const bool hot = (hoverLengthLane_ == l) || (dragMode_ == DragMode::laneLength && dragLane_ == l);
            canvas.setColor(colour::withAlpha(loop != patternSteps(pattern) ? colour::cyan : hue, hot ? 1.0f : 0.50f));
            canvas.roundedRectangle(hx - px(2.0f), ry + px(2.0f), px(4.0f), lay.rowH - px(4.0f), px(2.0f));
        }
    }

    // Playhead: the left edge of the step telemetry reports.
    if (playCol >= 0 && playCol < steps) {
        const float pxPos = lay.gridX + static_cast<float>(playCol) * lay.stepW;
        canvas.setColor(colour::withAlpha(colour::vermilion, 0.20f));
        canvas.rectangle(pxPos - px(3.0f), lay.gridY, px(6.0f), lay.gridH);
        canvas.setColor(colour::vermilion);
        canvas.rectangle(pxPos - px(1.0f), lay.gridY, px(2.0f), lay.gridH);
    }
}
```

`laneEndHandleX` clamps to `lay.steps` instead of `kMaxLaneSteps`.

`drawSlicerStage`: keep its layout and labels, with these changes. The gate strip reads `groove.bank.slicerSteps` and `groove.bank.slicerLength` directly (they are literal: a lit step is `step.on()`, its bar height is `step.velocity`, its badge is `step.ratchet` when that is 2 or more, and a strum mark when `step.flam`). There are `kMaxSlicerSteps` columns of `lay.slicerStepW`. Steps at or beyond the length are drawn as empty dimmed slots. Delete the "forceOff" cross. The play step is `(vis.position.barInPattern * 16 + vis.position.stepInBar) % slicerLength`. The note preview draws `vis.slicerPlan.notes[i]` using `beat / vis.slicerPlan.phraseBeats` for x, `durBeats` for width, `note` for height, and `voice == 0` under Bass / Stab for the vermilion bass colour. Replace the help text with `"Click: toggle\nDrag: velocity\nRight-click: ratchet, strum"` and the headline with `"HELD NOTES"`.

`draw`: compute `const Layout lay = computeLayout(width(), height(), gridSteps());`. The footer reads `"Click: step · Drag: velocity · Shift-click: ratchet · Right-click: layer"` in drum mode and `"MIDI Slicer: your held notes, sliced by the gate strip"` in slicer mode.

`ui/src/grid_stage_input.cpp`. Rewrite each handler on the model's bank. The shared helper:

```cpp
void GridStagePanel::edit(const std::function<bool(GrooveBank&)>& change) {
    GrooveBank bank = context_.model.groove().bank;
    if (change(bank))
        context_.model.setGrooveBank(bank);
}
```

`mouseMove`: as today, but the header hover tests are Clone (0), Clear (1), Fill (2) and the three pills (10, 11, 12); the lane and column under the mouse come from `grid::cellAt({lay.gridX, lay.gridY, lay.stepW, lay.rowH, lay.steps}, mx, my, lane, step)` (use the same call in `mouseDown` and `mouseDrag`; do not repeat the arithmetic in the panel); in the fill view the length handle is not hoverable; in slicer mode the column uses `lay.slicerStepW` and `kMaxSlicerSteps`.

`mouseDown`, drum mode, in this order:

1. Clone: `context_.model.cloneActiveSceneToNext()`. Clear: `context_.model.clearActiveScene()`. Fill: `context_.fillView = !context_.fillView; redraw();`.
2. A length pill with value `n` (1, 2 or 4): `edit([&](GrooveBank& b) { Pattern& p = b.scenes[scene]; if (fillView()) return grid::setFillBeats(p, n); if (patternBars(p) == n) return false; setPatternBars(p, n); return true; });`
3. Mute and solo pills toggle `muted` and `solo` on the lane.
4. The note badge: right-click sets `midiNote = 0`; otherwise start `DragMode::laneNote` with `dragBaseNote_ = effectiveLaneMidiNote(noteMap, lane, pattern.lanes[lane].midiNote)`.
5. Elsewhere in the sidebar: right-click opens `showLaneMenu(lane)`.
6. Near the loop handle (pattern view only): start `DragMode::laneLength`.
7. In the grid: if `grid::stepAt(pattern, fillView(), lane, step)` is null, return. Right-click opens `showDrumStepMenu(lane, step)`. Shift-click: if the step is off, `grid::toggleStep` it on first; then `grid::cycleRatchet`. Plain click on a lit step: start `DragMode::velocity` with `dragBaseVel_ = step.velocity` and `dragStartY_ = my`. Plain click on an off step: `grid::toggleStep` and start `DragMode::paintOn`.

`mouseDrag`:

- `laneLength`: `edit` with `grid::setLaneLength(p, dragLane_, lround((mx - lay.gridX) / lay.stepW))`.
- `laneNote`: as today, writing `midiNote` clamped to 24..96.
- `velocity`: ignore until `|dy| >= px(4)`, then `grid::setStepVelocity(*step, dragBaseVel_ + lround(dy * 1.4f))` on the drum step or the slicer step.
- `paintOn`: when the column changes, switch that step on if it is off (drum: `stepAt` not null and `!on()` then `toggleStep`; slicer: velocity 96).
- `slicerLength`: `bank.slicerLength = clamp(lround((mx - lay.gridX) / lay.slicerStepW), 1, kMaxSlicerSteps)`.

`mouseUp`: a velocity drag that moved less than `px(4)` switches the step off (`grid::toggleStep` for a drum step; `Step{}` for a slicer step). Then clear the drag state.

Slicer mode `mouseDown`: Clone and Clear in the header; the length handle; in the strip, a step at or beyond `slicerLength` does nothing; right-click opens `showSlicerStepMenu`; shift-click cycles ratchet on a lit step (lighting it first if needed); plain click starts a velocity drag on a lit step or lights an off one (`velocity = 96`) and starts `paintOn`.

`showDrumStepMenu(lane, step)`:

```cpp
void GridStagePanel::showDrumStepMenu(int lane, int step) {
    const bool fill = fillView();
    visage::PopupMenu menu;
    if (!fill) { // a fill step has no layer: if it is drawn, it sounds
        menu.addOption(20, "Layer: Core");
        menu.addOption(21, "Layer: Groove");
        menu.addOption(22, "Layer: Detail");
        menu.addBreak();
    }
    menu.addOption(1, "Ratchet: Off");
    menu.addOption(2, "Ratchet: ×2");
    menu.addOption(3, "Ratchet: ×3");
    menu.addOption(4, "Ratchet: ×4");
    menu.addBreak();
    menu.addOption(10, "Toggle Flam");
    menu.addOption(11, "Nudge Early");
    menu.addOption(12, "Nudge Late");
    menu.addOption(13, "Clear Step");
    menu.onSelection() += [this, lane, step, fill](int choice) {
        const int scene = editScene();
        edit([&](GrooveBank& bank) {
            Pattern& p = bank.scenes[static_cast<std::size_t>(scene)];
            Step* s = grid::stepAt(p, fill, lane, step);
            if (s == nullptr)
                return false;
            if (choice == 13) {
                *s = {};
                return true;
            }
            if (!s->on()) // every other choice needs a lit step: light it as a new step
                grid::toggleStep(p, fill, lane, step);
            if (choice >= 1 && choice <= 4)
                s->ratchet = static_cast<std::uint8_t>(choice);
            else if (choice == 10)
                s->flam = !s->flam;
            else if (choice == 11)
                s->nudge = static_cast<std::int8_t>(std::clamp(static_cast<int>(s->nudge) - 6, -24, 24));
            else if (choice == 12)
                s->nudge = static_cast<std::int8_t>(std::clamp(static_cast<int>(s->nudge) + 6, -24, 24));
            else if (choice >= 20 && choice <= 22)
                s->layer = static_cast<Layer>(choice - 20);
            return true;
        });
    };
    menu.show(this);
}
```

`showSlicerStepMenu(step)`: Ratchet Off, ×2, ×3, ×4; Toggle Chord Strum (`flam`); Clear Step. Same pattern, on `bank.slicerSteps[step]`, lighting an off step at velocity 96 first.

`showLaneMenu(lane)`: "Length: Follow Pattern" (`length = 0`), "Length: 5 steps", "Length: 7 steps", "Length: 12 steps" (through `grid::setLaneLength`), a break, "Reset Lane" (`lane = DrumLane{}`), "Reset All Notes to Kit Map" (`midiNote = 0` on every lane). The Euclidean entries are gone.

- [x] **Step 8: Port the pulse ribbon**

`ui/src/pulse_ribbon.cpp`: include `"grid_edit.h"`. In `draw`:

- The well has `steps = patternSteps(pattern)` columns in drum mode and `kMaxSlicerSteps` in slicer mode, where `pattern` is the scene being shown (`v.playing ? v.activeScene : the scene parameter`). Dividers fall every 4 steps; every 16th is a bar line.
- Drum mode: `const grid::View view = grid::resolveView(groove, scene, density, dynamics, liveFillBar);` with `liveFillBar` computed as in the grid. For each lane and column, read the cell with `grid::cellOf`. Skip `empty` and `outline` cells (the ribbon shows only what sounds). Draw the others as today, with `colour::amber` for `fill` cells. The loop-boundary tick and the `wrapped` dimming are gone: a looped lane is already resolved into every column.
- Slicer mode: draw `groove.bank.slicerSteps` up to `slicerLength`, as the grid's strip does.
- The playhead column is `v.position.barInPattern * 16 + v.position.stepInBar` in drum mode, and that modulo `slicerLength` in slicer mode. Draw the needle at the left edge of that column.
- The headline is `"SCN %c · %d bar"` / `"bars"` in drum mode (no style name), and the slicer style in slicer mode.

- [x] **Step 9: Port the PLAY panel**

`ui/src/play_panel.h` and `.cpp`:

- Remove `styleChip_`, `mutateBtn_` and `polyBtn_` (members, constructor initialisers, the child list, their accents and their `setBounds` calls). The dock keeps Hold, Quant, XY to CC, Kit map or Slicer, Audition and Drag .mid; close the gaps they leave by laying the remaining controls out left to right with the same spacing.
- `program_` and `programBefore_` start at 0.
- `tabRect` divides the width among `kPerfPadCount` tabs: `(width() - (kPerfPadCount - 1) * gap) / kPerfPadCount`.
- Wherever the panel read `v.playheadBeats`, read `v.ppq`. Wherever it read `v.plan.laneLengths`, delete the line (with `PadMotion::laneLens`).
- `formatAxisValue` becomes:

```cpp
std::string PlayPanel::formatAxisValue(int prog, bool isX, float v) const {
    char buf[32]{};
    if (isX) {
        switch (prog) {
        case 0: // Repeat: window
            return v < 0.25f ? "1 BEAT" : (v < 0.5f ? "1/2" : (v < 0.75f ? "1/4" : "1/8"));
        case 1: // Roll: rate
            return v < 0.25f ? "1/8" : (v < 0.5f ? "1/16" : (v < 0.75f ? "1/16T" : "1/32"));
        case 2: // Rate: speed
            return v < 1.0f / 3.0f ? "0.5×" : (v > 2.0f / 3.0f ? "2×" : "1×");
        default: // Drop
            return v < 0.5f ? "CUT" : "FILL";
        }
    }
    if (prog == 2)
        return v < 0.30f ? "KICK + SNARE" : (v > 0.70f ? "NO KICK" : "FULL KIT");
    if (prog == 3)
        return "";
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(v * 100.0f)));
    return buf;
}
```

  These thresholds are the ones in `engine/src/pad_programs.h`. Keep them equal.
- Update the header comment: four tabs, C3 to D#3.

`ui/src/top_bar.cpp` needs no change beyond compiling against the new `Visuals`; it already reads `kPerfPadCount`, `padProgram`, `seqRate`, `scene` and `engineMode`.

- [x] **Step 10: Open on GRID, and update the snapshot tool**

`ui/src/editor.cpp`: `bool gridMode = true;` in `Editor::Impl`. Nothing else changes: the macro strip lays out however many knobs it has.

`ui/include/cl/ui/editor.h` and `ui/src/editor.cpp`: add `void setFillView(bool fill);` to `Editor`. It sets `impl_->context.fillView` and redraws.

`ui/tools/snapshot.cpp`: the usage line becomes `usage: cl-ui-snapshot <out.png> [width height] [grid|grid1|grid4|live|fill|slicer|play|0..3]`. The default is the grid.

```cpp
    cl::ui::FakeModel model;
    bool showGrid = true;
    bool showFill = false;
    const std::string mode = modeArg != nullptr ? modeArg : "grid";
    if (mode == "grid1") {
        model.showBars(1);
    } else if (mode == "grid4") {
        model.showBars(4);
    } else if (mode == "live") {
        model.showLive();
    } else if (mode == "fill") {
        showFill = true;
    } else if (mode == "slicer") {
        model.showSlicer();
    } else if (mode != "grid") {
        showGrid = false; // PLAY: "play" keeps the model's program, a number selects one
        if (mode != "play")
            model.set(cl::ParamId::padProgram, static_cast<float>(std::atoi(mode.c_str())));
    }

    cl::ui::Editor editor(model);
    editor.setGridMode(showGrid);
    editor.setFillView(showFill);
    editor.renderToPng(argv[1], width, height);
    std::printf("%s %dx%d (%s)\n", argv[1], width, height, mode.c_str());
```

Add `#include <string>`.

Two things to know while porting:

- Write every non-ASCII character in a string literal as a `\u` escape (`"\u00d7"`, `"\u00b7"`, `"\u2194"`), as the existing UI code does.
- At 64 columns the cells are narrow: the ratchet badge is hidden (the `cw >= px(12)` test) and the ruler shows only the bar labels. That is expected for the minimal port.

- [x] **Step 11: Build the UI, run its tests, and look at it**

```bash
cmake --build --preset dev --target cl-ui-tests cl-ui-snapshot && build/dev/tests/cl-ui-tests
```

Expected: the build finishes with no warnings and the tests print `All tests passed`.

```bash
for mode in grid grid1 grid4 live fill slicer play 3; do build/dev/ui/cl-ui-snapshot build/dev/shot-$mode.png $mode; done
build/dev/ui/cl-ui-snapshot build/dev/shot-min.png 960 540 grid
```

Expected: nine lines naming the files. Open every PNG and check it against the fake model's pattern:

- `shot-grid.png`: the editor opens on GRID. The macro row has four knobs: Density, Swing, Dynamics, Fill. The grid has 32 columns with "BAR 1" and "BAR 2". The kick and hat lanes show three bar heights. At Density 60% some Detail steps are outlines. The snare's flam mark and the hat's `x2` badge are visible. PERC A shows a loop length of 7 and repeats across both bars. The header has Clone, Clear, Fill and `1 2 4` with `2` lit. There is no Mutate, Polymeter or lane lock.
- `shot-grid1.png`: 16 columns, `1` lit. `shot-grid4.png`: 64 columns, `4` lit, four bar labels, nothing overlapping.
- `shot-live.png`: the playhead is in bar 2; the last two beats of bar 2 are shaded amber and the fill's snare and kick steps are amber; the Repeat window tints one beat.
- `shot-fill.png`: 16 columns titled FILL, the last two beats shaded; only the drawn fill steps (snare and kick) are lit; the hat's main-pattern steps inside the window are dim outlines; the pills read `1 2 4` with `2` lit and the label "beats".
- `shot-slicer.png`: the gate strip has 32 columns with the default gate lit on steps 1, 4, 7, 11 and 15 of the first 16, and the length handle sits at the end of step 16 (half way along the strip), not at the far right. The header shows Clone and Clear.
- `shot-play.png`: four tabs, Repeat, Roll, Rate, Drop, labelled C3 to D#3; the ribbon across the top shows both bars. `shot-3.png`: the Drop tab is selected.
- `shot-min.png` (960 by 540, the minimum size): nothing is clipped or overlapping.

If the snapshot tool cannot create a window in this environment, say so in your report; do not skip the tests.

- [x] **Step 12: Check formatting and commit**

```bash
xcrun clang-format --style=file:../../.clang-format --dry-run --Werror \
  ui/include/cl/ui/model.h ui/include/cl/ui/fake_model.h ui/src/fake_model.cpp ui/src/grid_edit.h ui/src/grid_edit.cpp \
  ui/src/grid_stage.h ui/src/grid_stage_draw.cpp ui/src/grid_stage_input.cpp tests/ui/GridEditTests.cpp
```

Expected: no output. For the files edited in part (`pulse_ribbon.cpp`, `setup_panel.cpp`, `play_panel.*`, `pad_art.*`, `theme.h`, `editor.cpp`, `snapshot.cpp`), format only the lines you touched.

```bash
git add ui tests
git commit -m "feat(cutline)!: the grid and ribbon draw resolveBar; layer and fill editing; GRID opens first"
```

---

### Task 6: Plugin glue, factory presets, state versioning

When this task ends both presets are green.

**Files:**
- Modify: `plugin/src/PatternStore.h`, `plugin/src/PatternStore.cpp`, `plugin/src/Telemetry.h`, `plugin/src/PluginProcessor.h`, `plugin/src/PluginProcessor.cpp`, `plugin/src/PluginModel.h`, `plugin/src/PluginModel.cpp`, `plugin/src/Presets.h`, `plugin/src/Presets.cpp`, `plugin/src/MidiExporter.h`, `plugin/src/MidiExporter.cpp`, `plugin/src/PluginEditor.cpp`
- Create: `tools/build_presets.py`
- Replace: every file in `presets/`
- Create: `tests/core/FactoryPresetTests.cpp`
- Rewrite: `tests/plugin/PluginTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `GrooveState`, `Engine::setGroove`, `Engine::position`, `Engine::slicerPlan` (Tasks 3 and 4); `exportSmf` (Task 4); `parsePreset`, `serialisePreset`, `Preset` (Task 2); `parseGrooveBank`, `serialiseGrooveBank`, `kGrooveFormat` (`cl/groove_json.h`); `ui::Model`, `ui::Visuals` (Task 5).
- Produces, in `PatternStore`: `GrooveState get() const`; `GrooveBank bank() const`; `void set(const GrooveBank&)` (sanitises, ranks on the message thread, publishes, writes the state property); `void clear()` (the default bank); `bool reloadFromState()` (false when the stored bank is missing or does not parse; the store then holds the default bank); `std::uint32_t revision() const noexcept`; `bool pull(GrooveState&) noexcept`.
- Produces, in `PluginProcessor`: `void loadDefaultState()`; `HistorySnapshot::bank` is a `GrooveBank`. Plugin state carries `format="3"` on its root element and the bank in the `grooveBank` property.
- Produces: `presets/` holds exactly 16 files, all `"format": 3`.

- [x] **Step 1: Write the preset builder and build the presets**

Create `tools/build_presets.py`:

```python
#!/usr/bin/env python3
"""Builds the factory presets from the factory patterns.

Usage, from products/cutline:  python3 tools/build_presets.py

Writes one preset per file in patterns/ (the pattern in scene A, default parameters) and the five
MIDI Slicer presets, replacing everything in presets/. Run it again whenever a pattern changes;
tests/core/FactoryPresetTests.cpp fails if presets/ and patterns/ drift apart.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATTERNS = ROOT / "patterns"
PRESETS = ROOT / "presets"

CATEGORY = {
    "Amen": "Breaks", "Think": "Breaks", "Electro": "Breaks",
    "Neuro": "Neuro", "Halftime": "Neuro",
    "UKG": "Club", "Footwork": "Club", "Stepper": "Club", "Trap": "Club", "Drill": "Club", "Afro": "Club",
}

# The first bar of each 2.0 style's slicer mask: accents are X, other stabs x.
SLICER_GATE = {
    "Amen": "X..x..x...X...x.", "Think": "X.....x...X...x.", "Neuro": "X..x...x..X.x...",
    "Halftime": "X.....x....X....", "UKG": "X..x..X...x...X.", "Footwork": "X..x..X...x.X...",
    "Stepper": "x.Xx..X.x.Xx..X.", "Electro": "X..x..X...X.x...", "Drill": "X.xx..X..x.X..x.",
    "Afro": "X..x..X...x.X...", "Trap": "X......x..X...x.",
}

# The five 2.0 slicer presets, ported. Swing is 0.76 x the 2.0 Groove amount where the 2.0 pocket was
# MPC Swing (the two curves differ by that factor), and 0 otherwise.
SLICER_PRESETS = [
    ("slicer-ukg-chord-stabs", "Slicer: UKG Chord Stabs", "UKG",
     {"slicerStyle": "Stab Chop", "swing": 0.41, "gateLen": 0.38, "strumMs": 10.0, "slicerLatch": True}),
    ("slicer-liquid-strum", "Slicer: Liquid Strum", "Think",
     {"slicerStyle": "Strum / Flam", "swing": 0.24, "gateLen": 0.74, "strumMs": 22.0, "slicerLatch": True}),
    ("slicer-break-arp", "Slicer: Break Arp", "Amen",
     {"slicerStyle": "Break Arp", "swing": 0.20, "gateLen": 0.44, "strumMs": 8.0, "slicerLatch": True}),
    ("slicer-neuro-bass-stab", "Slicer: Neuro Bass + Stab", "Neuro",
     {"slicerStyle": "Bass / Stab", "gateLen": 0.52, "strumMs": 12.0, "slicerLatch": True}),
    ("slicer-live-clip-mangler", "Slicer: Live Clip Re-Slicer", "Drill",
     {"slicerStyle": "Clip Slice", "swing": 0.23, "gateLen": 0.62, "strumMs": 10.0}),
]


def load_patterns():
    patterns = {}
    for path in sorted(PATTERNS.glob("*.json")):
        data = json.loads(path.read_text())
        patterns[data["name"]] = data
    return patterns


def bank(pattern, gate):
    scene = {key: value for key, value in pattern.items() if key not in ("format", "name")}
    return {"scenes": [scene, {}, {}, {}], "slicer": {"length": 16, "steps": gate}}


def write(slug, preset):
    (PRESETS / f"{slug}.json").write_text(json.dumps(preset, indent=2, ensure_ascii=False) + "\n")


def main():
    patterns = load_patterns()
    missing = sorted(set(patterns) - set(CATEGORY))
    if missing:
        raise SystemExit(f"no category for pattern(s): {', '.join(missing)}")
    PRESETS.mkdir(exist_ok=True)
    for old in PRESETS.glob("*.json"):
        old.unlink()
    for name, pattern in patterns.items():
        write(name.lower(), {"format": 3, "name": name, "category": CATEGORY[name],
                             "bank": bank(pattern, SLICER_GATE[name])})
    for slug, name, source, params in SLICER_PRESETS:
        preset = {"format": 3, "name": name, "category": "MIDI Slicer", "engineMode": "MIDI Slicer"}
        preset.update(params)
        preset["bank"] = bank(patterns[source], SLICER_GATE[source])
        write(slug, preset)
    print(f"wrote {len(patterns) + len(SLICER_PRESETS)} presets to {PRESETS}")


if __name__ == "__main__":
    main()
```

Run it:

```bash
python3 tools/build_presets.py && ls presets | wc -l
```

Expected: `wrote 16 presets to .../products/cutline/presets`, then `16`. `git status --short presets` shows the 32 old files deleted and 16 added or modified.

- [x] **Step 2: Write the failing factory-content test**

Create `tests/core/FactoryPresetTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// The factory presets, checked against the factory patterns they are built from.
#include "cl/groove_json.h"
#include "cl/preset.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace cl;

namespace {

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::map<std::string, Preset> loadPresets() {
    std::map<std::string, Preset> presets;
    for (const auto& entry : std::filesystem::directory_iterator(CL_PRESETS_DIR)) {
        if (entry.path().extension() != ".json")
            continue;
        std::string error;
        const auto preset = parsePreset(slurp(entry.path()), &error);
        INFO(entry.path().filename().string() << ": " << error);
        REQUIRE(preset.has_value());
        REQUIRE(!preset->name.empty());
        REQUIRE(presets.emplace(preset->name, *preset).second); // names are unique
    }
    return presets;
}

} // namespace

TEST_CASE("There are 16 factory presets and every one is a Cutline 3 preset", "[presets]") {
    const auto presets = loadPresets();
    CHECK(presets.size() == 16);
    int slicer = 0;
    for (const auto& [name, preset] : presets) {
        INFO(name);
        CHECK((preset.category == "Breaks" || preset.category == "Neuro" || preset.category == "Club" ||
               preset.category == "MIDI Slicer"));
        slicer += preset.category == "MIDI Slicer" ? 1 : 0;
    }
    CHECK(slicer == 5);
}

TEST_CASE("Every factory pattern has a preset that plays it in scene A with default parameters", "[presets]") {
    const auto presets = loadPresets();
    const Params defaults;
    int patterns = 0;
    for (const auto& entry : std::filesystem::directory_iterator(CL_PATTERNS_DIR)) {
        if (entry.path().extension() != ".json")
            continue;
        ++patterns;
        std::string error;
        std::string name;
        const auto pattern = parsePattern(slurp(entry.path()), &error, &name);
        INFO(entry.path().filename().string() << ": " << error);
        REQUIRE(pattern.has_value());
        const auto found = presets.find(name);
        REQUIRE(found != presets.end());
        const Preset& preset = found->second;
        CHECK(preset.bank.scenes[0] == *pattern);
        for (std::size_t scene = 1; scene < preset.bank.scenes.size(); ++scene)
            CHECK(preset.bank.scenes[scene] == Pattern{});
        CHECK(preset.params.values == defaults.values);
        CHECK(preset.bank.slicerLength == 16);
        CHECK(preset.bank.slicerSteps[0].on());
    }
    CHECK(patterns == 11);
}

TEST_CASE("The slicer presets start in MIDI Slicer mode with a gate and a pattern", "[presets]") {
    const auto presets = loadPresets();
    for (const char* name : {"Slicer: UKG Chord Stabs", "Slicer: Liquid Strum", "Slicer: Break Arp",
                             "Slicer: Neuro Bass + Stab", "Slicer: Live Clip Re-Slicer"}) {
        INFO(name);
        const auto found = presets.find(name);
        REQUIRE(found != presets.end());
        const Preset& preset = found->second;
        CHECK(preset.params.choice(ParamId::engineMode) == static_cast<int>(EngineMode::slicer));
        CHECK(preset.bank.slicerSteps[0].on());
        CHECK(preset.bank.scenes[0].lanes[0].steps[0].on()); // a kick on beat 1 for Bass / Stab to follow
    }
    CHECK(presets.at("Slicer: Neuro Bass + Stab").params.choice(ParamId::slicerStyle) ==
          static_cast<int>(SlicerStyle::bassStab));
    CHECK(presets.at("Slicer: UKG Chord Stabs").params[ParamId::swing] > 0.3f);
}
```

In `tests/CMakeLists.txt`, add `core/FactoryPresetTests.cpp` to the `cl-core-tests` source list and, after its `target_link_libraries` line, add:

```cmake
target_compile_definitions(cl-core-tests PRIVATE
    CL_PATTERNS_DIR="${PROJECT_SOURCE_DIR}/patterns"
    CL_PRESETS_DIR="${PROJECT_SOURCE_DIR}/presets")
```

- [x] **Step 3: Run it**

```bash
cmake --build --preset engine --target cl-core-tests && build/engine/tests/cl-core-tests "[presets]"
```

Expected: `All tests passed (… assertions in 3 test cases)`. A failure here means `build_presets.py` and the codec disagree; read the `INFO` line for the file and the parser's reason.

- [x] **Step 4: Write the failing plugin tests**

Replace `tests/plugin/PluginTests.cpp` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"
#include "PluginProcessor.h"

#include "cl/groove_json.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <string>

using namespace cl;
using Catch::Approx;

namespace {
struct ScopedJuceGui {
    ScopedJuceGui() { juce::initialiseJuce_GUI(); }
    ~ScopedJuceGui() { juce::shutdownJuce_GUI(); }
};

// Moves a processor away from its defaults, so a reset is visible.
void disturb(PluginModel& model) {
    model.set(ParamId::density, 0.9f);
    model.set(ParamId::swing, 0.6f);
    GrooveBank bank = model.groove().bank;
    bank.scenes[0] = Pattern{};
    bank.scenes[2].lanes[3].steps[5].velocity = 96;
    model.setGrooveBank(bank);
}

void requireDefaultState(PluginProcessor& proc) {
    PluginModel model(proc);
    for (int i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        INFO(std::string(spec(id).key));
        CHECK(model.get(id) == Approx(spec(id).def).margin(1e-3f));
    }
    CHECK(model.groove().bank == GrooveBank{});
    CHECK(proc.curves.all() == defaultLaneCurves());
}
} // namespace

TEST_CASE("The factory presets load: patterns and slicer presets in four categories", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    const auto& all = proc.presets.all();
    std::set<std::string> categories;
    int factory = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].category == "User")
            continue; // presets saved on this machine
        ++factory;
        categories.insert(all[i].category);
        proc.presets.load(static_cast<int>(i));
        CHECK(proc.presets.currentName() == all[i].name);
        CHECK(proc.patterns.bank() == GrooveState(all[i].bank).bank);
    }
    CHECK(factory == 16);
    CHECK(categories == std::set<std::string>{"Breaks", "Neuro", "Club", "MIDI Slicer"});
}

TEST_CASE("A new instance opens on the default beat", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    requireDefaultState(proc);
    CHECK(proc.presets.currentName() == "Default Beat");
}

TEST_CASE("State round-trips parameters, curves and the groove bank", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor a;
    PluginModel modelA(a);
    modelA.set(ParamId::density, 0.72f);
    modelA.set(ParamId::fillEvery, 2.0f);
    modelA.setLaneCurve(1, curves::sine());

    GrooveBank bank = modelA.groove().bank;
    setPatternBars(bank.scenes[0], 2);
    bank.scenes[0].lanes[6].length = 7;
    bank.scenes[0].lanes[4].steps[15].velocity = 71;
    bank.scenes[0].lanes[4].steps[15].ratchet = 3;
    bank.scenes[0].fill.steps[1][14].velocity = 118;
    bank.slicerLength = 12;
    modelA.setGrooveBank(bank);
    const GrooveBank stored = modelA.groove().bank; // sanitised

    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);

    PluginProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    PluginModel modelB(b);
    CHECK(modelB.get(ParamId::density) == Approx(0.72f).margin(1e-3f));
    CHECK(static_cast<int>(modelB.get(ParamId::fillEvery) + 0.5f) == 2);
    CHECK(modelB.laneCurve(1) == curves::sine());
    CHECK(modelB.groove().bank == stored);
}

TEST_CASE("A Cutline 2 session loads the default state", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    {
        PluginModel model(proc);
        disturb(model);
    }

    // What Cutline 2 wrote: no format attribute, a 2.0 pattern bank, parameters that no longer exist.
    juce::XmlElement old("Cutline");
    old.setAttribute("patternBank", R"({"scenes":[{"custom":true}]})");
    old.setAttribute("presetName", "Amen Chop 172");
    auto* param = old.createNewChildElement("PARAM");
    param->setAttribute("id", "gravity");
    param->setAttribute("value", 0.72);
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(old, blob);

    proc.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    requireDefaultState(proc);
    CHECK(proc.presets.currentName() == "Default Beat");
}

TEST_CASE("State that does not parse loads the default state", "[plugin]") {
    ScopedJuceGui gui;

    PluginProcessor garbage;
    {
        PluginModel model(garbage);
        disturb(model);
    }
    const char junk[] = "this is not plugin state";
    garbage.setStateInformation(junk, static_cast<int>(sizeof(junk)));
    requireDefaultState(garbage);

    // The right format number, but a bank that is not a bank.
    PluginProcessor broken;
    {
        PluginModel model(broken);
        disturb(model);
    }
    juce::XmlElement xml("Cutline");
    xml.setAttribute("format", 3);
    xml.setAttribute("grooveBank", R"({"scenes":[{"bars":3}]})");
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(xml, blob);
    broken.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    requireDefaultState(broken);
}

TEST_CASE("Undo, redo, clone and clear work on the groove bank", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    PluginModel model(proc);

    model.begin(ParamId::density);
    model.set(ParamId::density, 0.85f);
    model.end(ParamId::density);
    REQUIRE(model.canUndo());
    model.undo();
    CHECK(model.get(ParamId::density) == Approx(spec(ParamId::density).def).margin(1e-3f));
    REQUIRE(model.canRedo());
    model.redo();
    CHECK(model.get(ParamId::density) == Approx(0.85f).margin(1e-3f));

    const Pattern sceneA = model.groove().bank.scenes[0];
    model.cloneActiveSceneToNext();
    CHECK(static_cast<int>(model.get(ParamId::scene) + 0.5f) == 1);
    CHECK(model.groove().bank.scenes[1] == sceneA); // an exact copy: nothing is mutated

    model.clearActiveScene();
    CHECK(model.groove().bank.scenes[1] == Pattern{});
    CHECK(model.groove().bank.scenes[0] == sceneA);
    model.undo();
    CHECK(model.groove().bank.scenes[1] == sceneA);

    CHECK(model.exportStatus() == "Drag 1-bar .mid");
}

TEST_CASE("The model serves a sanitised bank with the engine's density maps", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    PluginModel model(proc);
    GrooveBank bank = model.groove().bank;
    bank.scenes[0].lanes[0].steps[40].velocity = 96; // beyond a one-bar pattern
    bank.scenes[0].lanes[4].steps[3].velocity = 56;
    bank.scenes[0].lanes[4].steps[3].layer = Layer::detail;
    model.setGrooveBank(bank);

    const GrooveState& groove = model.groove();
    CHECK_FALSE(groove.bank.scenes[0].lanes[0].steps[40].on());
    CHECK(groove.maps[0].onAt[0][0] == Approx(0.0f)); // Core
    CHECK(groove.maps[0].onAt[4][3] == Approx(1.0f)); // the only Detail step arrives at 100%
    CHECK(groove.bank == proc.patterns.bank());
}

TEST_CASE("processBlock renders finite audition audio", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i) {
        buf.clear();
        midi.clear();
        proc.processBlock(buf, midi);
        for (int ch = 0; ch < 2; ++ch) {
            const float* data = buf.getReadPointer(ch);
            for (int s = 0; s < 256; ++s)
                REQUIRE(std::isfinite(data[s]));
        }
    }
}
```

- [x] **Step 5: Run them to verify they fail**

```bash
cmake --build --preset dev --target cl-plugin-tests
```

Expected: the build fails in `plugin/src` (`PatternBank` is not declared).

- [x] **Step 6: Port the pattern store**

Replace `plugin/src/PatternStore.h` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// The plugin's groove bank. Edits arrive on the message thread; set() sanitises the bank, ranks every
// scene's density map there, and publishes both to the audio thread through a try-lock mailbox.
#pragma once

#include "cl/groove_state.h"

#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>

namespace cl {

class PatternStore {
  public:
    explicit PatternStore(juce::AudioProcessorValueTreeState& state);

    GrooveState get() const;
    GrooveBank bank() const;
    void set(const GrooveBank& bank);
    void clear();

    // Reads the bank from plugin state. Returns false when it is missing or does not parse;
    // the store then holds the default bank.
    bool reloadFromState();

    // Bumps on every set, clear and reload, so the UI knows when to fetch the state again.
    std::uint32_t revision() const noexcept { return revision_.load(std::memory_order_relaxed); }

    bool pull(GrooveState& destination) noexcept { return mailbox_.pull(destination); }

  private:
    void publish(const GrooveState& groove);

    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<GrooveState> mailbox_{};
    std::atomic<std::uint32_t> revision_{0};
};

} // namespace cl
```

Replace `plugin/src/PatternStore.cpp` with:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PatternStore.h"

#include "Parameters.h"
#include "cl/groove_json.h"

namespace cl {

namespace {
const juce::Identifier kGrooveBankProp{"grooveBank"};
} // namespace

PatternStore::PatternStore(juce::AudioProcessorValueTreeState& state) : state_(state) {}

GrooveState PatternStore::get() const { return mailbox_.get(); }

GrooveBank PatternStore::bank() const { return mailbox_.get().bank; }

void PatternStore::publish(const GrooveState& groove) {
    mailbox_.set(groove);
    revision_.fetch_add(1, std::memory_order_relaxed);
}

void PatternStore::set(const GrooveBank& bank) {
    const GrooveState groove(bank); // sanitises and ranks, here on the message thread
    publish(groove);
    state_.state.setProperty(kGrooveBankProp, toJuce(serialiseGrooveBank(groove.bank)), nullptr);
}

void PatternStore::clear() { set(GrooveBank{}); }

bool PatternStore::reloadFromState() {
    const auto text = state_.state.getProperty(kGrooveBankProp).toString();
    if (text.isNotEmpty()) {
        if (const auto parsed = parseGrooveBank(text.toStdString())) {
            publish(GrooveState(*parsed));
            return true;
        }
    }
    publish(GrooveState());
    return false;
}

} // namespace cl
```

- [x] **Step 7: Port telemetry, the processor and state versioning**

`plugin/src/Telemetry.h`: delete the constructor (nothing is compiled up front any more). `publish` takes the mode and copies position state instead of a phrase plan:

```cpp
    // Audio thread: never blocks; skips the update if the UI is mid-copy.
    void publish(const Engine& engine, double bpm, bool slicerMode) noexcept {
        lastBpm_.store(bpm, std::memory_order_relaxed);
        const juce::SpinLock::ScopedTryLockType scoped(lock_);
        if (!scoped.isLocked())
            return;
        snapshot_.position = engine.position();
        if (slicerMode)
            snapshot_.slicerPlan = engine.slicerPlan(); // the slicer keeps its plan snapshot
        snapshot_.heldNotes = engine.activeHeldNotes();
        snapshot_.activeScene = engine.activeScene();
        snapshot_.pendingScene = engine.pendingScene();
        snapshot_.activePadProgram = engine.activePadProgram();
        snapshot_.pendingPadProgram = engine.pendingPadProgram();
        snapshot_.padEngaged = engine.padEngaged();
        snapshot_.midiPadMask = engine.midiPadMask();
        snapshot_.capturedClipCount = engine.capturedClipCount();
        snapshot_.bpm = bpm;
        snapshot_.ppq = engine.lanePpq();
        snapshot_.lanePhase = engine.lanePhases();
        snapshot_.laneValue = engine.laneValues();
        snapshot_.laneFlash = engine.laneHitFlashes();
        snapshot_.activeNotesLow = engine.activeNotesLowMask();
        snapshot_.activeNotesHigh = engine.activeNotesHighMask();
        snapshot_.lastBlockEvents = engine.lastBlockEventCount();
        snapshot_.activityMask |= engine.lastActivityMask();
        snapshot_.playing = engine.isPlaying();
    }
```

`plugin/src/PluginProcessor.h`: `HistorySnapshot::bank` becomes `GrooveBank bank{};`. `audioBank_` becomes `GrooveState audioGroove_{};`. Add the public declaration `void loadDefaultState();`.

`plugin/src/PluginProcessor.cpp`:

- In `processBlock`, replace the pattern pull and the telemetry call:

```cpp
    if (patterns.pull(audioGroove_))
        engine_.setGroove(audioGroove_);
```

```cpp
    telemetry.publish(engine_, transport.bpm, params.choice(ParamId::engineMode) == static_cast<int>(EngineMode::slicer));
```

- Replace the two state functions and add `loadDefaultState`:

```cpp
namespace {
const juce::Identifier kFormatProp{"format"};
const juce::Identifier kGrooveBankProp{"grooveBank"}; // the same property PatternStore writes
} // namespace

// Saving must not change the plugin: hosts call this often, and not always on the message thread.
// Stamp the format and the bank on a COPY of the state, so nothing is re-published to the audio thread.
void PluginProcessor::getStateInformation(juce::MemoryBlock& destination) {
    juce::ValueTree copy = state.copyState();
    copy.setProperty(kFormatProp, kGrooveFormat, nullptr);
    copy.setProperty(kGrooveBankProp, toJuce(serialiseGrooveBank(patterns.bank())), nullptr);
    if (const auto xml = copy.createXml())
        copyXmlToBinary(*xml, destination);
}

// Cutline 3 state carries format="3". Anything else (a Cutline 2 session, or bytes that do not parse)
// loads the default state, as the spec says.
void PluginProcessor::setStateInformation(const void* data, int size) {
    const auto xml = getXmlFromBinary(data, size);
    const bool current = xml != nullptr && xml->hasTagName(state.state.getType()) &&
                         xml->getIntAttribute(kFormatProp.toString(), 0) >= kGrooveFormat;
    if (current) {
        state.replaceState(juce::ValueTree::fromXml(*xml));
        curves.reloadFromState();
        if (patterns.reloadFromState())
            return;
    }
    loadDefaultState();
}

void PluginProcessor::loadDefaultState() {
    for (const auto& spec : kParams) {
        if (auto* parameter = state.getParameter(toJuce(spec.key)))
            parameter->setValueNotifyingHost(parameter->getDefaultValue());
    }
    curves.setAll(defaultLaneCurves());
    patterns.clear();
    presets.clearCurrentName();
}
```

Add `#include "cl/groove_json.h"` for `kGrooveFormat`.

Add one plugin test for this, beside the others in `tests/plugin/PluginTests.cpp`: "Saving state does not republish the bank": read `proc.patterns.revision()`, call `getStateInformation` twice, and check the revision is unchanged.

- [x] **Step 8: Port presets, export and the model**

`plugin/src/Presets.h` and `.cpp`:

- `kDefaultPreset` becomes `"Default Beat"`.
- `categoryRank` orders `"Breaks", "Neuro", "Club", "MIDI Slicer"`.
- `load` calls `patterns_.set(preset.bank)`; `save` reads `preset.bank = patterns_.bank()`.
- Add `void clearCurrentName();`, which removes the `presetName` property from the state.

`plugin/src/MidiExporter.h` and `.cpp`: `writeTempFile` takes `const GrooveBank& bank` and calls `exportSmf(params, curves, held, bpm, sceneIdx, bank)`. Include `"cl/groove.h"` and `"cl/held_notes.h"` instead of `"cl/pattern.h"` and `"cl/phrase.h"`.

`plugin/src/PluginModel.h`: replace `patternBank()` / `setPatternBank()` with

```cpp
    const GrooveState& groove() const override;
    void setGrooveBank(const GrooveBank& bank) override;
```

remove `mutateActiveScene`, `polymeterActiveScene` and `actionCounter_`, and add the cache members

```cpp
    mutable GrooveState groove_{};
    mutable std::uint32_t grooveRevision_ = 0xffffffffu; // never equal to the store's first revision
    SlicerPlan previewPlan_{};     // the slicer phrase shown while the transport is stopped
    SlicerInputs previewInputs_{}; // what previewPlan_ was compiled from
```

`plugin/src/PluginModel.cpp`:

```cpp
// The model keeps a copy of the published GrooveState and refetches it only when the store changed.
const GrooveState& PluginModel::groove() const {
    const std::uint32_t revision = processor_.patterns.revision();
    if (revision != grooveRevision_) {
        groove_ = processor_.patterns.get();
        grooveRevision_ = revision;
    }
    return groove_;
}

void PluginModel::setGrooveBank(const GrooveBank& bank) {
    if (!applyingSnapshot_)
        processor_.history.pushCoalesced(captureSnapshot(), 0x2000u,
                                         static_cast<std::int64_t>(juce::Time::currentTimeMillis()));
    processor_.patterns.set(bank);
}

void PluginModel::refreshVisuals() {
    processor_.telemetry.read(visuals_);
    if (visuals_.playing)
        return;
    // Stopped: show what the parameters say will play from the top of the pattern.
    Params p;
    for (int i = 0; i < kParamCount; ++i)
        p[static_cast<ParamId>(i)] = get(static_cast<ParamId>(i));
    const int scene = std::clamp(p.choice(ParamId::scene), 0, kSceneCount - 1);
    const double bpm = processor_.telemetry.lastBpm();
    visuals_.activeScene = scene;
    visuals_.pendingScene = -1;
    visuals_.activePadProgram = std::clamp(p.choice(ParamId::padProgram), 0, kPerfPadCount - 1);
    visuals_.pendingPadProgram = -1;
    visuals_.padEngaged = p.on(ParamId::padTouch) || p.on(ParamId::padHold);
    visuals_.position = {};
    visuals_.bpm = bpm;
    if (p.choice(ParamId::engineMode) == static_cast<int>(EngineMode::slicer)) {
        // The preview phrase: recompiled only when something it depends on changed, not on every UI tick.
        const GrooveState& state = groove();
        const SlicerInputs now = slicerInputs(p, scene, visuals_.heldNotes, bpm, grooveRevision_);
        if (!now.sameAs(previewInputs_)) {
            compileSlicer(p, state, scene, visuals_.heldNotes, bpm, previewPlan_);
            previewInputs_ = now;
        }
        visuals_.slicerPlan = previewPlan_;
    }
}

void PluginModel::cloneActiveSceneToNext() {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    const int cur = std::clamp(static_cast<int>(get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
    const int next = (cur + 1) % kSceneCount;
    GrooveBank bank = processor_.patterns.bank();
    bank.scenes[static_cast<std::size_t>(next)] = bank.scenes[static_cast<std::size_t>(cur)];
    processor_.patterns.set(bank);
    begin(ParamId::scene);
    set(ParamId::scene, static_cast<float>(next));
    end(ParamId::scene);
}

// A cleared scene is empty and stays empty. In MIDI Slicer mode, Clear empties the gate strip instead.
void PluginModel::clearActiveScene() {
    if (!applyingSnapshot_)
        processor_.history.push(captureSnapshot());
    const int scene = std::clamp(static_cast<int>(get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
    GrooveBank bank = processor_.patterns.bank();
    if (static_cast<int>(get(ParamId::engineMode) + 0.5f) == static_cast<int>(EngineMode::slicer)) {
        bank.slicerLength = 16;
        bank.slicerSteps = {};
    } else {
        bank.scenes[static_cast<std::size_t>(scene)] = Pattern{};
    }
    processor_.patterns.set(bank);
}

std::string PluginModel::exportStatus() const {
    const int scene = std::clamp(static_cast<int>(get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
    return "Drag " + std::to_string(patternBars(groove().bank.scenes[static_cast<std::size_t>(scene)])) + "-bar .mid";
}
```

In `dragMidiPhrase`, pass `processor_.patterns.bank()`. In `captureSnapshot`, `s.bank = processor_.patterns.bank();`. `applySnapshot` is unchanged (`processor_.patterns.set(snapshot.bank)`). Include `"cl/phrase.h"` for `compileSlicer`.

The clone test undoes a clear with one `undo()`. `cloneActiveSceneToNext` pushes history once and then `begin(ParamId::scene)` pushes again, as in 2.0; that is fine for the test, which only undoes the clear.

`plugin/src/PluginEditor.cpp`: the About text described the 2.0 generator. Replace the three strings:

```cpp
    cfg.accessibilitySuffix = ". Drum patterns you can read, and a MIDI slicer.";
    cfg.headline = "Drum patterns you can read, and a MIDI slicer";
    cfg.body = "The grid is what plays: literal steps in three layers, one Density knob, fills you draw, and four "
               "scenes. Switch to MIDI Slicer to chop, strum and re-groove your own chords and clips.";
```

- [x] **Step 9: Build and test both presets**

```bash
cmake --build --preset engine && ctest --preset engine 2>&1 | tail -3
```

Expected: `100% tests passed, 0 tests failed out of 89` (86 plus the 3 factory-preset cases).

```bash
cmake --build --preset dev && ctest --preset dev 2>&1 | tail -3
```

Expected: `100% tests passed`. The `dev` count is the 89 above, plus the UI tests (the curve-edit cases that were already there and 7 grid cases), plus 8 plugin cases and the Mac About test. Report the number you get.

- [x] **Step 10: Render the real plugin's editor state through the snapshot tool**

```bash
build/dev/ui/cl-ui-snapshot build/dev/grid.png grid
```

Expected: the same picture as in Task 5 (the snapshot tool uses the fake model; this is a regression check that the UI still builds and renders after the plugin work).

- [x] **Step 11: Check formatting and commit**

```bash
xcrun clang-format --style=file:../../.clang-format --dry-run --Werror \
  plugin/src/PatternStore.h plugin/src/PatternStore.cpp tests/plugin/PluginTests.cpp tests/core/FactoryPresetTests.cpp
```

Expected: no output. Format only the touched lines of the other plugin files.

```bash
git add -A plugin presets tools tests
git commit -m "feat(cutline)!: plugin state, telemetry and factory presets on the groove model"
```

---

### Task 7: Keep the factory showcase compiling

CI builds `factory/showcase` against every product's engine (`.github/workflows/factory.yml`, "Verify studio-showcase compiles against all 6 engines"). It uses the Cutline engine API and names 2.0 presets and pad programs, so it stops compiling at Task 2. This task makes it compile and run against Cutline 3. Re-rendering the website's demo audio is out of scope: do not commit any audio.

**Files:**
- Modify: `factory/showcase/clips_cutline.cpp` (repository root)

**Interfaces:**
- Consumes: `cl::Engine::setGroove`, `cl::GrooveState`, `cl::parsePreset`, `cl::PadProgram::{repeat, roll, rate, drop}`, `cl::ParamId::swing`, the 16 preset files from Task 6.
- Produces: nothing new.

- [x] **Step 1: Port the file**

Run from the repository root. In `factory/showcase/clips_cutline.cpp`:

- Add `#include "cl/groove_state.h"`.
- Every `clEng.setPatternBank(clParsed->bank);` becomes `clEng.setGroove(cl::GrooveState(clParsed->bank));`.
- Preset file names: `amen-chop-172.json` becomes `amen.json`; `strike-body-neuro-snare.json` becomes `neuro.json`; `afro-7-11-polyrhythm.json` becomes `afro.json`; `halftime-85-colossus.json` becomes `halftime.json`. The two slicer presets keep their names. The clip that used `strike-body-neuro-snare.json` drives the Strike Body engine, and `neuro.json` uses the default kit map: after loading it, set `clP[cl::ParamId::noteMap] = static_cast<float>(cl::NoteMap::strikeBody);` so the notes still reach Strike Body's articulations.
- Pad programs: `cl::PadProgram::poly`, `slice` and `stutter` become `cl::PadProgram::repeat`; `bounce` becomes `cl::PadProgram::roll`; `roll` and `drop` keep their names.
- `clParams[cl::ParamId::gravity] = ...` becomes `clParams[cl::ParamId::swing] = ...` with the same expression scaled into 0..1 (multiply by 0.76 and clamp).
- Any other use of a removed parameter (`chop`, `ghosts`, `complexity`, `fills`, `ratchet`, `style`, `variation`, `fillType`, `attractor`, `phraseBars`) is deleted. Where a clip relied on one to build intensity, automate `cl::ParamId::density` over the same span instead.
- Change the section comment from "CUTLINE 2.0" to "CUTLINE 3". Leave the clip titles and descriptions passed to `emitClip` alone: they are website copy.

- [x] **Step 2: Build the showcase**

```bash
cmake -S factory/showcase -B factory/showcase/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPM_SOURCE_CACHE="$HOME/.cache/CPM" && cmake --build factory/showcase/build --parallel
```

Expected: the build finishes. This compiles all the studio's engines and takes several minutes; run it in the background. If it fails in a file other than `clips_cutline.cpp`, check `git status`: nothing else should have changed.

- [x] **Step 3: Run the factory unit tests**

```bash
python3 -m unittest discover -s factory/release 2>&1 | tail -3
```

Expected: `OK`. (`test_recall.py` only checks that the recall fixture exists; the fixture itself is regenerated at release time. See "Deferred, with reasons".)

- [x] **Step 4: Commit**

```bash
git add factory/showcase/clips_cutline.cpp
git commit -m "build(showcase): compile the Cutline clips against the Cutline 3 engine"
```

---

### Task 8: README, spec sync and the full verification pass

**Files:**
- Rewrite: `README.md`
- Modify: `docs/specs/2026-11-26-groove-model-design.md`, `docs/plans/2026-11-26-groove-model-2-cutover.md` (this file: check the boxes and record what changed during execution)
- Modify: `engine/include/cl/engine.h`, `tools/cutline_render.cpp`, `plugin/CMakeLists.txt` only where a comment or description still says "Cutline 2.0", "polymeter" or "12 styles"

**Interfaces:**
- Consumes: everything above.
- Produces: a branch on which both presets are green and the documents match the code.

- [x] **Step 1: Rewrite the README**

Replace `README.md`. Keep the existing file's build, install and licence sections as they are (commands, paths and the licence do not change). Replace everything that describes what the product is and does with a description of Cutline 3, in this order and no longer than the 2.0 text it replaces:

1. One sentence: Cutline is a drum-pattern MIDI plugin whose grid is exactly what plays, plus a MIDI slicer for your own chords and clips.
2. **The grid is the truth.** Literal steps in three layers (Core, Groove, Detail). Nothing in playback is random. One function, `resolveBar`, decides what sounds, and both the engine and the grid call it.
3. **Controls.** Density (0% Core, 50% Core and Groove, 100% everything), Swing, Dynamics, Fill (Off, or every 2, 4, 8 or 16 bars), Scene A to D, Rate. State the defaults: Density 50%, Swing 0%, Dynamics 50%, Fill Off.
4. **Fills.** Drawn, off by default; a lane with nothing drawn in the fill keeps playing the main pattern.
5. **Pads (interim).** Repeat, Roll, Rate, Drop, with their X and Y, and that Pad MIDI (C3 to D#3) and XY to CC are off by default.
6. **MIDI Slicer.** The literal gate strip; Stab Chop, Strum / Flam, Break Arp, Bass / Stab, Clip Slice.
7. **Factory content.** 11 patterns in `patterns/`, 16 presets in `presets/` built by `tools/build_presets.py`.
8. **Files.** Patterns and presets are JSON with `"format": 3`. Cutline 2 presets are rejected, and a Cutline 2 session opens on the default beat.
9. **For developers.** The `engine` and `dev` presets, `cutline-render`, `cl-ui-snapshot`, and where the spec and plans live.

Do not mention: styles as a live parameter, Ghosts, Chop, Ratchet or Complex macros, Mutate, Polymeter, lane Lock, Euclidean, eight pad programs, phrase length. Do not add release notes or a version number.

- [x] **Step 2: Sync the spec with what was built**

In `docs/specs/2026-11-26-groove-model-design.md`:

- Status line: "Plan 1 implemented on branch `cutline-3-groove-model`, Plan 2 on branch `cutline-3-cutover`".
- Pattern data rules: a new bank also holds the default slicer gate `x..x..x...x...x.`. Add one rule: "Banks are kept in canonical form by `sanitise`: off steps are blank, fields are in range, and steps hidden by the pattern length, the fill window or the slicer length are cleared."
- File format: an edit with a missing or out-of-range `step`, or one that targets a step that is off, is an error, in patterns and in the slicer alike. A fill edit before the fill window is still ignored.
- MIDI export: the clip is `bars * 4 / rate` beats long, and a hit that starts before bar 1 or after the last bar is wrapped to the other end of the clip.
- Pad programs: one sentence on what the pads do in MIDI Slicer mode (Repeat loops the phrase's notes, Roll retriggers the held notes, Drop's right half does nothing).
- UI: the grid header has `1 | 2 | 4` pills for bars and, in the fill view, for fill beats.
- Resolve / Timing, from the engine review rounds in Task 3. Add, in the engine's own terms:
  - Scene switches: a step plays from the scene that is active at the step's grid time, so the old scene's late hits and the new scene's early hits both sound. Known limits: a switch quantized to 1/16 or 1/4 cannot play a hit that should have started before the switch was requested, and only one switch boundary is remembered at a time.
  - Shift moves the whole pattern, and scene and pad switches fall on the shifted pattern's grid.
  - `fillNow` and the fill schedule are latched together, once per bar.
  - Two lanes that share a MIDI note and land on the same sample send one note-on, with the higher velocity (live and in export).
  - Note-offs are sent in time order with note-ons, and a block never sends more note-ons than leave room for the note-offs they need.
- Pad programs: pads engage on the next 1/16 and release at once (already added in Task 3); in the block where a pad engages, the pattern plays up to the switch point.
- Out of scope, release work: add "the release recall fixture `factory/release/fixtures/cutline.vststate`, which must be regenerated before Cutline 3 ships".

Change nothing else in the spec.

- [x] **Step 3: Sweep for 2.0 wording in code comments**

```bash
grep -rnE "Cutline 2|2\.0|polymet|Polymeter|12 (multi-bar|styles)|Mutate|Euclid" core engine plugin ui tools tests CMakeLists.txt | grep -v "^docs/"
```

Expected after fixing: no line that describes current behaviour in 2.0 terms. Lines that state history on purpose ("Cutline 2 preset: not supported in Cutline 3", a test named for a Cutline 2 session, "as in 2.0" in a comment explaining a port) stay. `CMakeLists.txt`'s `VERSION 2.0.0` stays: the version bump is release work.

- [x] **Step 4: Run every test in both presets from clean object files**

```bash
cmake --build --preset engine --clean-first && ctest --preset engine 2>&1 | tail -3
```

```bash
cmake --build --preset dev && ctest --preset dev 2>&1 | tail -3
```

Expected: `100% tests passed` twice. Record both counts in your report.

- [x] **Step 5: Check the done criteria one by one**

```bash
test ! -e engine/src/styles.cpp && test ! -e engine/src/styles.h && test ! -e tools/seed_patterns.py && echo "generator deleted"
grep -rnE "laneHash|Rng\b|uniform\(|rand\(" core engine/src/engine.cpp engine/src/engine_impl.h engine/src/pad_programs.cpp engine/src/phrase.cpp engine/src/resolve.cpp engine/src/smf.cpp engine/include; echo "random grep exit: $?"
grep -rn "triggerNote(" engine/src | grep -v "void Engine::Impl::triggerNote\|engine_impl.h"
ls presets | wc -l
```

Expected: `generator deleted`; the random grep prints nothing and `random grep exit: 1`; `triggerNote(` is called from exactly one place, `emitBlockHits`, which is fed only by `playPattern`, `playRepeat`, `playRoll` (drum hits through `resolveBar` and `forEachHit`, or the Roll pad's snare note) and the slicer functions; `16`.

- [x] **Step 6: Check formatting across everything this plan touched**

```bash
git diff --name-only --relative main -- '*.h' '*.cpp' | grep -v third_party | xargs xcrun clang-format --style=file:../../.clang-format --dry-run --Werror 2>&1 | grep -c "error:" || true
```

Expected: `0` for files this plan created or rewrote. For files it edited in part, an error on a line this plan did not touch is not this plan's to fix; an error on a touched line is. Fix what is yours, rebuild, and rerun Step 4 if anything changed.

- [x] **Step 7: Record the outcome in this plan and commit**

Tick the boxes in this file. The second `git status` in the block below must show nothing left unstaged under `products/cutline`: Step 3's comment edits live in source files, and `git add -u` stages them. Under a new last section, "What changed during execution", list every place the committed code differs from this plan's code blocks and why, and under "Not verified here" list at least: RealtimeSanitizer (CI only), `git-clang-format` against the pinned clang-format (CI only), a DAW session (Ableton Live: bank 1 mapping, transport loop, pre-roll, scene quantize, MIDI drag), anything heard (nobody has listened to the patterns, the fills, Swing or the pads), Windows and Linux builds, and the release recall fixture.

```bash
git status --short -- .
git add README.md docs
git add -u -- core engine plugin ui tools tests CMakeLists.txt
git status --short -- .
git commit -m "docs(cutline): describe Cutline 3 and sync the spec and plan with what was built"
```

---

## What comes after this plan

- The grid spec (interaction design, the layer brush, the phrase strip), the library spec (the listening pass, the wider pattern set, Strike Body presets, per-preset Swing) and the PLAY spec.
- Release work: the version bump, release notes, the recall fixture, website copy and demo audio.

## What changed during execution

The plan was executed task by task on `cutline-3-cutover`, one implementer per task, each followed by a review. The committed code differs from the code blocks above in the places listed here. The spec was kept as the authority throughout: where the plan's code disagreed with it, the code was changed to match the spec.

### Plan review by a second session

While Tasks 1 to 3 were running, a second session reviewed the whole plan in three parts. Its findings were folded in before the tasks they affected.

- **Spec coverage.** The grid draws the scene being edited and hides the playhead, the live fill and the Repeat window until the engine's active scene matches it. Clone stays in the slicer header. Pad quantize became one named value with a test at default parameters. The fill latch is indexed so that a block spanning several bars cannot mix up their decisions. The per-block hit limit was raised to the MIDI buffer's capacity, and a large-block truth test was added.
- **Engine.** Most items went to Task 3's fix rounds (below). Two went into Task 4's text: Clip Slice visits every bar and every Repeat cell, with a test across bar lines; and MIDI export merges notes of the same pitch on the same tick.
- **Cutover mechanics.** The randomness greps leave out the audition synth's noise source. Task 8 stages the comment edits in source files. The fill view lights only fill steps. The UI snapshot tool gained modes (`grid1`, `grid4`, `live`, `fill`, `slicer`). The plugin saves its state from a copy, with a test that saving does not move the bank's revision. `SlicerInputs` was added to `phrase.h` so that the engine and the plugin's preview cache decide in the same way when the slicer phrase must be recompiled.

### Founder decision: pads at 1/16

Pads engage, and switch program, on the next 1/16 note, whatever the scene quantize is, and release at once. There is no new parameter; it is one named constant, `kPadQuantizeBeats`. Scene quantize now applies to scene switches only. The spec says so in "Pad programs (interim)" and "Unchanged".

### Task 1

`maxLateBeats`, the furthest a hit can land after its step, now includes a swing allowance (`kMaxSwingBeats = 0.125`). The plan's bound could not pass its own test: swing delays odd steps, so a late ratchet can land past the end of its step on the grid. The allowance is generous; it is used only to choose which bars a block must look at.

### Task 3: three fix rounds

The review of Task 3 found that a note-off could cut a retriggered note at large block sizes, and that a pad or scene switch took over the whole block. The fixes:

- Note-offs are sent in time order with note-ons, so a note-off for an earlier note never cuts the next note at the same pitch.
- Two lanes that share a MIDI note and land on the same sample send one note-on, with the higher velocity.
- Repeat re-anchors its window after a transport jump.
- Each step plays from the scene that owns its grid time, so a scene switch inside a block plays the old scene up to the switch and the new scene after it. A pad engaging inside a block takes over at its switch point, not at the start of the block.
- Note-ons never use up the room kept for the note-offs they need.
- The whole fill decision (the schedule or `fillNow`) is latched once per bar.
- Playback, scene switches, pad switches and the reported position all use one clock: the transport moved by Shift.
- The second round found that the Drop pad's flush of sounding notes ran before the hits that came earlier in the block, which could send a note-on twice. Drop's flush now happens in time order. A third round closed the case where the switch point falls exactly on the end of the block.

### Task 4

The review found that Bass / Stab ignored the gate step. The spec says the gate is literal, so the code was changed rather than the spec. Bass / Stab now honours the gate step's ratchet, nudge, flam and velocity, and the kick step's ratchet and nudge. Clip Slice's Repeat loops from its anchor rather than from the start of the slice. Tests now pin the slicer's Repeat and Roll note positions and run the slicer with large blocks at 172 BPM and 2x.

### Task 5

- A click on a lane that loops a shorter length now edits the stored step that plays there, not a hidden step past the loop.
- A drag is not a click: a velocity drag that returns to its start no longer deletes the step.
- The fill view lights only the fill's own steps.
- The snapshot tool's modes, listed above, were used to check the editor.

### Task 6

The plugin writes its saved state onto a copy, so saving changes nothing in the running plugin. The slicer preview in the editor is cached and recompiled only when its `SlicerInputs` change.

### Known limits that were accepted

- A scene switch quantized to 1/16 or 1/4 cannot play a hit that should have started before the switch was requested.
- Only one scene switch boundary is remembered at a time.
- In MIDI Slicer mode, a scene switch inside a block plays the new scene's phrase for that whole block.
- Cutline 2 user presets are not loaded and no message is shown. `Presets::scan` reads user presets through the shared `hba::host::appendUserPresets`, which drops a file that does not parse together with its error, so `kOldFormatError` never reaches anyone: a Cutline 2 preset simply does not appear in the list. The shared module was left alone. Whether to show a message is a decision for the founder.
- While the transport runs on, a small forward skip (up to 1/8 beat) or Shift moving suddenly earlier is played, not dropped: the skipped stretch sounds on the block's first sample. It is bounded (under half a beat of hits at the fastest tempo and a full Shift swing), same-note hits merge, and the MIDI stays well-formed.
- A tempo step in the middle of one large block can still read as a loop (at 4096 samples and 48 kHz, a drop of more than about 11 BPM), which flushes and can double one hit. Hosts normally change tempo on block boundaries.
- With all 32 voices sounding and a 33rd note starting on the same sample as the note it steals, that stolen note can be left hanging. Drum mode cannot reach it (a kit map has at most 8 notes); MIDI Slicer mode with a very large held chord could.

### Minor items left for the whole-branch review

The final fix round below resolved these entries of the list: the `setPatternBars` short-lane test, the kit-map comment, the `silenced` flag, the PLAY dock's Quant chip, the `getStateInformation` comment, `kGrooveBankProp` defined twice, and the untested state paths (wrong root tag, missing bank). The whole-branch review judged these not worth doing: the merge's velocity-0 marker, the kept lane's flash, the refused-note-on audition test, `kMaxSwingBeats` over-reserving, the unused include, `serialisePreset` always writing the bank, `reloadFromState` on failure, and the drag label (it refreshes every tick). Everything else stays open for the grid, library, PLAY and release work.

- `kMaxSwingBeats` reserves more than it needs (about 0.03 beats would do) and repeats `swingBeat`'s 0.125.
- The clamp limits (24, 20, 127, 4) are written out in `groove.h` and `groove_json.cpp`, while `hits.h` has `kMaxNudge` and `kMaxOffsetMs`.
- The JSON reader reads `step` with `get<int>()`, so a huge unsigned value can wrap and pass. A non-array `"edits"` is ignored without an error.
- No test covers `setPatternBars` growing a pattern that has a short lane and a step repeat.
- `parsePreset` and plugin state accept any `format` of 3 or more, so a future format 4 would parse.
- The kit-map comment in `pattern.h` still mentions 2.0 preset compatibility.
- Note 60 is "C3" in the pad hints but `midiNoteLabel(60)` returns "C4" (an older mismatch).
- `CoreTests.cpp` includes `cl/lanes.h` without using it. `serialisePreset` always writes `"bank"`.
- The same-note merge uses velocity 0 as its marker, and only the kept lane flashes after a merge. A merged hit keeps the kept hit's pad flag.
- The scene-switch test checks early hits only for the 1 Bar quantize. No test shows that a refused note-on starts no audition voice. The pad tests' open-note count no longer catches stray note-offs. Drop's flush is tested only with the switch point exactly on the block's end.
- The `silenced` flag in the engine reads inverted. Lane telemetry (phases and values) still uses the clock without Shift.
- `slicerPlan()` returns a reference into audio-thread state and only updates while playing. Tempo ramps recompile the slicer phrase every block. The exporter pulls the last note back by 16 ticks.
- Bass / Stab: a flammed step gets the +8 top-note accent (untested); the kick's nudge, the flam's reversal and the phrase-end clamp are untested. The Clip Repeat test uses one note at the slice start. The 2.0 "-8 on voice 1" velocity tweak is gone.
- UI: the mouse handlers are tested only through the pure helpers. `DragGesture` has no test for an upward-only drag or a drag exactly at the threshold. No test covers `FakeModel::clearActiveScene` emptying the gate strip in MIDI Slicer mode. The PLAY dock still shows the Quant chip, which now governs scenes only, and its drag label is set at construction. At 64 columns the loop handle's hit zone is about 4.5 px either side.
- Plugin: `getStateInformation`'s comment overstates how safe it is off the message thread. `reloadFromState` leaves unparseable text in the tree on failure and publishes twice. History is not cleared on a state load. Clone takes two undos. The telemetry's slicer plan goes stale after leaving slicer mode. `kGrooveBankProp` is defined in two files. A wrong root tag and a missing `grooveBank` are untested.
- Showcase: clip 1's two pad stages both use Repeat, and `emitClip` titles still say "Cutline 2.0", "Poly", "Bounce" and "Stutter" (website copy).

### Final fix round (whole-branch review)

The whole-branch review returned "merge with fixes". This round made them:

- **Contiguous block windows.** While the transport runs on, each block's window starts where the last one ended, in the shifted window clock, so under tempo or Shift automation every pattern instant is played exactly once; on a start, loop or jump it starts at the host position as before. A step back of up to 1/64 beat counts as tempo automation, not a loop (a host integrates the tempo, while a block's length in beats uses the tempo at its start). A hit that falls before the block's first sample is sent on that sample and keeps its full length from there, and a hit that would start on the sample where Drop silences is not started. A test plays the default beat under a 30 BPM/s ramp at 64, 512 and 2048-sample blocks, and another automates Shift.
- **Fill latch on a Rate change.** The latches are cleared when the block's effective rate (Rate times the Rate pad) differs from the previous block's, so a bar latched at one rate no longer plays its fill at another.
- **Tests.** The truth tests' expected list applies the engine's same-note, same-sample merge, with an explicit test of a step nudged onto its neighbour. A seeded fuzz in drum and MIDI Slicer mode, a 4-bar loop test, a plugin test with a playing transport (default beat, an edit, a state load), state with a wrong root tag or no bank, and a test that pins `setPatternBars` on a looped lane.
- **PLAY.** The dock has a Pad MIDI toggle in place of the Quant chip (scene quantize stays in the GRID setup panel). The tabs' key hints and the badge's "MIDI C3" show only while Pad MIDI is on. The fill view's footer no longer mentions layers.
- **Clean-ups.** The engine's `silenced` flag is now `silencePending`. `kGrooveBankProp` is defined once. Comments corrected: `setPatternBars`, the kit-map order, `getStateInformation`'s thread safety, and the showcase's clip 3.

### Open questions for the founder

- The pattern plays during host pre-roll at negative positions, including a count-in. Cutline 2 was silent before beat 0; the spec only says hit times are not clamped.
- Bypassing the plugin while a slicer note sounds may leave that note hanging: `processBlockBypassed` is not overridden (the same as 2.0).
- A host loop whose length the pattern does not divide (a 3-bar loop over a 2-bar pattern): the pattern follows the transport position, so each pass cuts the pattern short and starts again from the loop start's position. Nothing was decided for it.
- All grid edits share one undo tag, so two different edits within the coalescing window undo together.
- Every drag tick serialises the whole bank into the state tree.
- Whether to tell the user that a Cutline 2 preset was not loaded (see "Known limits that were accepted").
- The PLAY tabs show the pad key hints whenever Pad MIDI is on, including in MIDI Slicer mode, where Pad MIDI does nothing.

### Not verified here

- RealtimeSanitizer. The `rtsan` preset is Linux-only and runs in CI. Here, only `-Werror=function-effects` was checked, by compiling.
- `git-clang-format` against the pinned clang-format. That runs in CI. Locally, Xcode's `clang-format` was used.
- Any DAW session. Nothing was tried in Ableton Live or any other host: the bank 1 mapping on Push, a transport loop, pre-roll, scene quantize, pad MIDI, MIDI drag and drop, and state recall in a real project.
- Anything heard. Nobody has listened to the patterns, the fills, Swing, the pads, the slicer or the showcase clips. Audio reading was not available in the app used for this work.
- Mouse interaction with the real editor. The grid's logic is unit-tested, and the editor was checked through rendered snapshots of a fake model, not by clicking.
- Windows and Linux builds.
- The release recall fixture `factory/release/fixtures/cutline.vststate`. It predates Cutline 3 and must be regenerated before release.
- A Cutline 2 session opened in a real host. The test builds Cutline 2 state by hand.
