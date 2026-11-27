# Cutline 3 Groove Model, Plan 1 of 2: Foundation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the Cutline 3 groove model (literal steps in three layers, one drawn fill), the single `resolveBar` function that decides what sounds, its timing and JSON code, and 11 linted factory patterns, without changing how Cutline 2.0 behaves yet.

**Architecture:** Everything here is additive. New headers sit beside the 2.0 code and nothing existing calls them, so both CMake presets stay green after every task. Plan 2 then cuts the engine, plugin and UI over to these pieces and deletes the 2.0 generator.

**Tech Stack:** C++20, CMake presets, Catch2, nlohmann/json through `hba::core::Json`, Python 3 for the one-off pattern converter.

**Spec:** [`docs/specs/2026-11-26-groove-model-design.md`](../specs/2026-11-26-groove-model-design.md)

## Global Constraints

- Work on branch `cutline-3-groove-model`, never on `main`.
- All commands run from `products/cutline` unless a step says otherwise.
- This plan adds files and edits three `CMakeLists.txt` files. It must not change any other existing file. Cutline 2.0 behaviour, presets and tests stay exactly as they are.
- Every new C++ file starts with `// SPDX-License-Identifier: AGPL-3.0-or-later`.
- Studio targets compile with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.
- Every function marked `CL_NONBLOCKING` must be allocation-free, lock-free and syscall-free, and must pass `-Wfunction-effects -Werror=function-effects`.
- No random numbers anywhere in the new code.
- 4/4 only: 16 steps per bar, 4 beats per bar.
- Format only the lines you add. Never reformat an untouched file.
- Commit messages have no attribution trailer lines.
- Lane order is fixed: 0 kick, 1 snare, 2 ghost, 3 clap, 4 closed hat, 5 open hat, 6 perc A, 7 perc B.

The code in this plan was compiled with those flags and its test cases were run against the 11 generated patterns on 2026-11-26, outside the repository. The code blocks below match what was committed, including two fixes made during execution and one rule change made after the final review. Task 4's reader and writer drop fill steps that sit before the fill window, as the spec requires. Task 3's `forEachHit` no longer clamps hit times to beat 0. Task 2's `resolveBar` lets a lane with nothing drawn in the fill window keep playing the main pattern. If a step's code fails to compile as written, stop and report it instead of improvising.

## File Structure

| File | Responsibility |
|------|----------------|
| `core/include/cl/groove.h` | The data model: `Layer`, `Step`, `DrumLane`, `Fill`, `Pattern`, `GrooveBank`, the default beat, and length helpers. Header-only. |
| `engine/include/cl/resolve.h`, `engine/src/resolve.cpp` | `resolveBar`: pattern + Density + Dynamics + fill flag in, the steps that sound out. Also the rank comparator and the cached `DensityMap`. |
| `engine/include/cl/hits.h` | Timing: swing, fill schedule, and `forEachHit`, which turns a resolved bar into timed note-ons. Header-only. |
| `core/include/cl/groove_json.h`, `core/src/groove_json.cpp`, `core/src/groove_json_detail.h` | Reading and writing patterns and banks as JSON. The detail header exposes the `Json`-level functions to the rest of `cl_core`. |
| `tools/seed_patterns.py` | One-off converter from the 2.0 style tables to pattern files. Deleted in Plan 2 with `styles.cpp`. |
| `patterns/*.json` | The 11 factory seed patterns. |
| `tests/core/GrooveTests.cpp`, `tests/core/GrooveJsonTests.cpp` | Model and JSON tests, in `cl-core-tests`. |
| `tests/engine/ResolveTests.cpp`, `tests/engine/HitsTests.cpp`, `tests/engine/PatternLintTests.cpp` | Resolve, timing and factory-content tests, in `cl-engine-tests`. |

`cl/pattern.h` already defines `kDrumLaneCount` and the kit maps. `groove.h` includes it and reuses them. The 2.0 types in that header (`StepEdit`, `LaneConfig`, `SceneData`, `PatternBank`) do not clash with the new names and are removed in Plan 2.

## Before you start

- [ ] **Create the branch and commit the spec and this plan**

Run from `products/cutline`:

```bash
git switch -c cutline-3-groove-model
git add docs/specs/2026-11-26-groove-model-design.md docs/plans/2026-11-26-groove-model-1-foundation.md
git commit -m "docs(cutline): add Cutline 3 groove-model spec and foundation plan"
```

- [ ] **Configure the engine preset once**

```bash
cmake --preset engine -DCPM_SOURCE_CACHE="$HOME/.cache/CPM"
```

Expected: `-- Build files have been written to: .../products/cutline/build/engine`

---

### Task 1: Groove data model

**Files:**
- Create: `core/include/cl/groove.h`
- Create: `tests/core/GrooveTests.cpp`
- Modify: `core/CMakeLists.txt` (source list)
- Modify: `tests/CMakeLists.txt:1` (the `cl-core-tests` source list)

**Interfaces:**
- Consumes: `kDrumLaneCount` from `cl/pattern.h`, `kSceneCount` from `cl/params.h`, `CL_NONBLOCKING` from `cl/nonblocking.h`.
- Produces: `kStepsPerBar` (16), `kMaxPatternBars` (4), `kMaxPatternSteps` (64), `kMaxSlicerSteps` (32), `kLaneKeys`, `enum class Layer { core, groove, detail }`, `struct Step { velocity, layer, ratchet, nudge, flam; bool on() }`, `struct DrumLane { steps[64], length, midiNote, offsetMs, muted, solo }`, `struct Fill { steps[8][16], beats }`, `struct Pattern { bars, lanes[8], fill }`, `struct GrooveBank { scenes[4], slicerLength, slicerSteps[32] }`, `Pattern defaultBeat()`, `int patternBars(const Pattern&)`, `int patternSteps(const Pattern&)`, `int laneLoopSteps(const Pattern&, int lane)`, `int storedStep(const Pattern&, int lane, int stepInPattern)`, `int fillWindowStart(const Fill&)`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/GrooveTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove.h"

#include <catch2/catch_test_macros.hpp>

using namespace cl;

TEST_CASE("A new groove bank holds the default beat in scene A and empty patterns in B-D", "[groove]") {
    const GrooveBank bank;
    const Pattern& a = bank.scenes[0];
    CHECK(patternBars(a) == 1);
    for (int step = 0; step < kStepsPerBar; ++step) {
        const auto si = static_cast<std::size_t>(step);
        CHECK(a.lanes[0].steps[si].on() == (step == 0 || step == 8));
        CHECK(a.lanes[1].steps[si].on() == (step == 4 || step == 12));
        CHECK(a.lanes[4].steps[si].on() == (step % 2 == 0));
    }
    CHECK(a.lanes[0].steps[0].velocity == 96);
    CHECK(a.lanes[1].steps[4].layer == Layer::core);
    for (std::size_t scene = 1; scene < bank.scenes.size(); ++scene)
        CHECK(bank.scenes[scene] == Pattern{});
    CHECK(bank.slicerLength == 16);
}

TEST_CASE("Pattern length helpers clamp bars and loop short lanes", "[groove]") {
    Pattern p{};
    CHECK(patternSteps(p) == 16);
    p.bars = 2;
    CHECK(patternSteps(p) == 32);
    p.bars = 3;
    CHECK(patternBars(p) == 2);
    p.bars = 4;
    CHECK(patternSteps(p) == 64);

    CHECK(laneLoopSteps(p, 6) == 64);
    p.lanes[6].length = 7;
    CHECK(laneLoopSteps(p, 6) == 7);
    CHECK(storedStep(p, 6, 0) == 0);
    CHECK(storedStep(p, 6, 7) == 0);
    CHECK(storedStep(p, 6, 23) == 2);
    p.lanes[6].length = 64;
    CHECK(laneLoopSteps(p, 6) == 64);
}

TEST_CASE("The fill window covers the last 1, 2 or 4 beats of the bar", "[groove]") {
    Fill fill{};
    fill.beats = 1;
    CHECK(fillWindowStart(fill) == 12);
    fill.beats = 2;
    CHECK(fillWindowStart(fill) == 8);
    fill.beats = 4;
    CHECK(fillWindowStart(fill) == 0);
    fill.beats = 3;
    CHECK(fillWindowStart(fill) == 8);
}

TEST_CASE("The groove bank stays small enough to copy on the audio thread", "[groove]") {
    CHECK(sizeof(GrooveBank) < 16384);
}
```

In `tests/CMakeLists.txt`, change line 1 from

```cmake
add_executable(cl-core-tests core/CoreTests.cpp)
```

to

```cmake
add_executable(cl-core-tests core/CoreTests.cpp core/GrooveTests.cpp)
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset engine --target cl-core-tests
```

Expected: the build fails with `fatal error: 'cl/groove.h' file not found`.

- [ ] **Step 3: Write the model**

Create `core/include/cl/groove.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline 3 groove model: literal steps in three layers, one drawn fill per pattern.
// Fixed-size and allocation-free, so it can be copied on the real-time audio thread.
#pragma once

#include "cl/nonblocking.h"
#include "cl/params.h"
#include "cl/pattern.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace cl {

inline constexpr int kStepsPerBar = 16;
inline constexpr int kMaxPatternBars = 4;
inline constexpr int kMaxPatternSteps = kStepsPerBar * kMaxPatternBars;
inline constexpr int kMaxSlicerSteps = 32;

// JSON keys for the eight drum lanes, in lane order.
inline constexpr std::array<std::string_view, kDrumLaneCount> kLaneKeys{"kick", "snare",   "ghost", "clap",
                                                                        "hat",  "openHat", "percA", "percB"};

enum class Layer : std::uint8_t { core = 0, groove = 1, detail = 2 };

struct Step {
    std::uint8_t velocity = 0; // 0 = off, 1..127 = on
    Layer layer = Layer::groove;
    std::uint8_t ratchet = 1; // 1..4 evenly spaced hits inside the step
    std::int8_t nudge = 0;    // -24..+24, units of 1/96 beat
    bool flam = false;        // one grace hit 20 ms early

    constexpr bool on() const noexcept CL_NONBLOCKING { return velocity > 0; }
    bool operator==(const Step&) const noexcept = default;
};

struct DrumLane {
    std::array<Step, kMaxPatternSteps> steps{};
    std::uint8_t length = 0;   // 0 = follow the pattern; 1..64 = loop this many steps
    std::uint8_t midiNote = 0; // 0 = kit map note; 1..127 = override
    std::int8_t offsetMs = 0;  // -20..+20, whole-lane pocket
    bool muted = false;
    bool solo = false;

    bool operator==(const DrumLane&) const noexcept = default;
};

struct Fill {
    std::array<std::array<Step, kStepsPerBar>, kDrumLaneCount> steps{};
    std::uint8_t beats = 2; // 1, 2 or 4: how much of the bar's tail it replaces

    bool operator==(const Fill&) const noexcept = default;
};

struct Pattern {
    std::uint8_t bars = 1; // 1, 2 or 4
    std::array<DrumLane, kDrumLaneCount> lanes{};
    Fill fill{};

    bool operator==(const Pattern&) const noexcept = default;
};

// Kick on 1 and 3, snare on 2 and 4, closed hat on every 8th. Every hit is Core at velocity 96.
constexpr Pattern defaultBeat() noexcept CL_NONBLOCKING {
    Pattern p{};
    const auto put = [&p](int lane, int step) {
        Step& s = p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
        s.velocity = 96;
        s.layer = Layer::core;
    };
    put(0, 0);
    put(0, 8);
    put(1, 4);
    put(1, 12);
    for (int step = 0; step < kStepsPerBar; step += 2)
        put(4, step);
    return p;
}

struct GrooveBank {
    std::array<Pattern, kSceneCount> scenes{}; // scenes A-D
    std::uint8_t slicerLength = 16;            // 1..32
    std::array<Step, kMaxSlicerSteps> slicerSteps{};

    // A new bank holds the default beat in scene A and empty patterns in B-D.
    constexpr GrooveBank() noexcept CL_NONBLOCKING { scenes[0] = defaultBeat(); }

    bool operator==(const GrooveBank&) const noexcept = default;
};

// 1, 2 or 4. Any other stored value rounds down to the nearest of those.
constexpr int patternBars(const Pattern& p) noexcept CL_NONBLOCKING { return p.bars >= 4 ? 4 : (p.bars >= 2 ? 2 : 1); }

constexpr int patternSteps(const Pattern& p) noexcept CL_NONBLOCKING { return patternBars(p) * kStepsPerBar; }

// How many stored steps a lane cycles through: its own length when that is shorter than the pattern.
constexpr int laneLoopSteps(const Pattern& p, int lane) noexcept CL_NONBLOCKING {
    const int steps = patternSteps(p);
    const int length = static_cast<int>(p.lanes[static_cast<std::size_t>(lane)].length);
    return (length >= 1 && length < steps) ? length : steps;
}

// The stored step a lane plays at `stepInPattern` (0 .. patternSteps - 1).
constexpr int storedStep(const Pattern& p, int lane, int stepInPattern) noexcept CL_NONBLOCKING {
    return stepInPattern % laneLoopSteps(p, lane);
}

// First step of the bar that the fill replaces: 12, 8 or 0 for 1, 2 or 4 beats.
constexpr int fillWindowStart(const Fill& fill) noexcept CL_NONBLOCKING {
    const int beats = fill.beats >= 4 ? 4 : (fill.beats <= 1 ? 1 : 2);
    return kStepsPerBar - beats * 4;
}

} // namespace cl
```

In `core/CMakeLists.txt`, add the header to the `cl_core` source list, after `include/cl/curve.h`:

```cmake
    include/cl/groove.h
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build --preset engine --target cl-core-tests && ./build/engine/tests/cl-core-tests "[groove]"
```

Expected: `All tests passed (` followed by an assertion count, `in 4 test cases)`.

- [ ] **Step 5: Commit**

```bash
git add core/include/cl/groove.h core/CMakeLists.txt tests/core/GrooveTests.cpp tests/CMakeLists.txt
git commit -m "feat(cutline): add the Cutline 3 groove data model"
```

---

### Task 2: resolveBar, the single source of truth

**Files:**
- Create: `engine/include/cl/resolve.h`
- Create: `engine/src/resolve.cpp`
- Create: `tests/engine/ResolveTests.cpp`
- Modify: `engine/CMakeLists.txt` (source list)
- Modify: `tests/CMakeLists.txt` (the `cl-engine-tests` source list)

**Interfaces:**
- Consumes: everything Task 1 produces.
- Produces: `struct ResolvedStep { velocity, ratchet, nudge, flam, written, fromFill, layer }`, `struct ResolvedBar { lanes[8][16] }`, `struct ResolveInput { float density; float dynamics; bool fillActive; }`, `struct DensityMap { float onAt[8][64] }`, `bool rankBefore(int laneA, int stepA, int laneB, int stepB)`, `void buildDensityMap(const Pattern&, DensityMap&)`, `std::uint8_t applyDynamics(int velocity, float dynamics)`, `void resolveBar(const Pattern&, const DensityMap&, int barInPattern, const ResolveInput&, ResolvedBar&)`, and `void resolveBar(const Pattern&, int barInPattern, const ResolveInput&, ResolvedBar&)`.

`rankBefore` is the one musical rule in the engine: it decides which hit arrives first as Density rises. It is written out below as the spec defines it. Keep it as a single function so it can be tuned by ear later without touching anything else.

- [ ] **Step 1: Write the failing test**

Create `tests/engine/ResolveTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/resolve.h"

#include <catch2/catch_test_macros.hpp>

using namespace cl;

namespace {

Step hit(int velocity, Layer layer) {
    Step s;
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = layer;
    return s;
}

int sounding(const ResolvedBar& bar) {
    int n = 0;
    for (const auto& lane : bar.lanes) {
        for (const auto& s : lane)
            n += s.velocity > 0 ? 1 : 0;
    }
    return n;
}

// One Core kick, two Groove hits, two Detail hits.
Pattern layered() {
    Pattern p{};
    p.lanes[0].steps[0] = hit(96, Layer::core);
    p.lanes[0].steps[10] = hit(96, Layer::groove);
    p.lanes[4].steps[2] = hit(96, Layer::groove);
    p.lanes[4].steps[3] = hit(96, Layer::detail);
    p.lanes[2].steps[7] = hit(56, Layer::detail);
    return p;
}

} // namespace

TEST_CASE("Density brings Groove in up to 50% and Detail in up to 100%", "[engine][resolve]") {
    const Pattern p = layered();
    ResolvedBar bar;

    resolveBar(p, 0, {0.0f, 0.5f, false}, bar);
    CHECK(sounding(bar) == 1);
    CHECK(bar.lanes[0][0].velocity > 0);
    CHECK(bar.lanes[0][10].written);
    CHECK(bar.lanes[0][10].velocity == 0);

    resolveBar(p, 0, {0.25f, 0.5f, false}, bar);
    CHECK(sounding(bar) == 2);
    CHECK(bar.lanes[0][10].velocity > 0); // kick outranks hat at the same metric weight

    resolveBar(p, 0, {0.5f, 0.5f, false}, bar);
    CHECK(sounding(bar) == 3);
    resolveBar(p, 0, {0.75f, 0.5f, false}, bar);
    CHECK(sounding(bar) == 4);
    resolveBar(p, 0, {1.0f, 0.5f, false}, bar);
    CHECK(sounding(bar) == 5);
}

TEST_CASE("Raising Density never removes a hit and Core always sounds", "[engine][resolve]") {
    const Pattern p = layered();
    ResolvedBar bar;
    int previous = 0;
    for (int d = 0; d <= 100; ++d) {
        resolveBar(p, 0, {static_cast<float>(d) / 100.0f, 0.5f, false}, bar);
        const int now = sounding(bar);
        CHECK(now >= previous);
        CHECK(bar.lanes[0][0].velocity > 0);
        previous = now;
    }
    CHECK(previous == 5);
}

TEST_CASE("Hits rank by beat strength, then lane priority, then position", "[engine][resolve]") {
    CHECK(rankBefore(0, 0, 0, 8));       // downbeat before beat 3
    CHECK(rankBefore(4, 8, 0, 4));       // beat 3 before beat 2, whatever the lane
    CHECK(rankBefore(0, 4, 1, 4));       // kick before snare
    CHECK(rankBefore(4, 2, 2, 2));       // closed hat before ghost
    CHECK(rankBefore(0, 2, 0, 18));      // bar 1 before bar 2
    CHECK_FALSE(rankBefore(0, 3, 7, 2)); // an odd 16th never outranks an even one
}

TEST_CASE("Dynamics scales velocity contrast around 64 and is the identity at 50%", "[engine][resolve]") {
    CHECK(applyDynamics(96, 0.5f) == 96);
    CHECK(applyDynamics(96, 0.0f) == 80);
    CHECK(applyDynamics(96, 1.0f) == 112);
    CHECK(applyDynamics(120, 1.0f) == 127);
    CHECK(applyDynamics(1, 1.0f) == 1);
}

TEST_CASE("A live fill replaces only the lanes drawn in it and ignores Density", "[engine][resolve]") {
    Pattern p = layered();
    p.lanes[4].steps[14] = hit(96, Layer::core);
    p.lanes[1].steps[13] = hit(96, Layer::core);
    p.fill.beats = 1;
    p.fill.steps[1][14] = hit(118, Layer::groove);
    ResolvedBar bar;

    resolveBar(p, 0, {1.0f, 0.5f, false}, bar);
    CHECK(bar.lanes[4][14].velocity > 0);
    CHECK(bar.lanes[1][13].velocity > 0);
    CHECK_FALSE(bar.lanes[1][14].written);

    resolveBar(p, 0, {0.0f, 0.5f, true}, bar);
    CHECK(bar.lanes[1][14].velocity == 118); // the drawn lane takes the fill, whatever the Density
    CHECK(bar.lanes[1][14].fromFill);
    CHECK_FALSE(bar.lanes[1][13].written); // and loses its own steps inside the window
    CHECK(bar.lanes[4][14].velocity > 0);  // a lane with nothing drawn in the fill keeps playing
    CHECK_FALSE(bar.lanes[4][14].fromFill);
    CHECK(bar.lanes[0][0].velocity > 0); // outside the window the main pattern plays
}

TEST_CASE("An empty fill changes nothing, and steps drawn before the window do not count", "[engine][resolve]") {
    Pattern p = layered();
    p.lanes[4].steps[14] = hit(96, Layer::core);
    p.fill.beats = 1;
    p.fill.steps[4][2] = hit(96, Layer::groove); // before the 1-beat window
    ResolvedBar plain;
    ResolvedBar filled;
    resolveBar(p, 0, {1.0f, 0.5f, false}, plain);
    resolveBar(p, 0, {1.0f, 0.5f, true}, filled);
    for (std::size_t lane = 0; lane < plain.lanes.size(); ++lane) {
        for (std::size_t step = 0; step < plain.lanes[lane].size(); ++step) {
            CHECK(filled.lanes[lane][step].velocity == plain.lanes[lane][step].velocity);
            CHECK_FALSE(filled.lanes[lane][step].fromFill);
        }
    }
}

TEST_CASE("Muted and non-soloed lanes stay written but silent", "[engine][resolve]") {
    Pattern p = layered();
    ResolvedBar bar;

    p.lanes[0].muted = true;
    resolveBar(p, 0, {1.0f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].written);
    CHECK(bar.lanes[0][0].velocity == 0);

    p.lanes[0].muted = false;
    p.lanes[4].solo = true;
    resolveBar(p, 0, {1.0f, 0.5f, false}, bar);
    CHECK(bar.lanes[0][0].velocity == 0);
    CHECK(bar.lanes[4][2].velocity > 0);
}

TEST_CASE("A lane with a shorter length loops across the bar", "[engine][resolve]") {
    Pattern p{};
    p.lanes[6].length = 3;
    p.lanes[6].steps[0] = hit(96, Layer::core);
    ResolvedBar bar;
    resolveBar(p, 0, {0.0f, 0.5f, false}, bar);
    for (int step = 0; step < kStepsPerBar; ++step)
        CHECK((bar.lanes[6][static_cast<std::size_t>(step)].velocity > 0) == (step % 3 == 0));
}

TEST_CASE("The cached density map gives the same result as the convenience overload", "[engine][resolve]") {
    const Pattern p = layered();
    DensityMap map;
    buildDensityMap(p, map);
    CHECK(map.onAt[0][0] == 0.0f);
    CHECK(map.onAt[0][1] == 2.0f);
    ResolvedBar a;
    ResolvedBar b;
    for (int d = 0; d <= 10; ++d) {
        const ResolveInput in{static_cast<float>(d) / 10.0f, 0.5f, false};
        resolveBar(p, 0, in, a);
        resolveBar(p, map, 0, in, b);
        CHECK(sounding(a) == sounding(b));
    }
}
```

In `tests/CMakeLists.txt`, add the file to `cl-engine-tests`, directly after `engine/EngineTests.cpp`:

```cmake
    engine/ResolveTests.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset engine --target cl-engine-tests
```

Expected: the build fails with `fatal error: 'cl/resolve.h' file not found`.

- [ ] **Step 3: Write the header**

Create `engine/include/cl/resolve.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// The single source of truth for what sounds. The engine emits drum notes only from a
// ResolvedBar, and the UI draws only from a ResolvedBar it computes with the same function.
// Pure, stateless, allocation-free.
#pragma once

#include "cl/groove.h"
#include "cl/nonblocking.h"

#include <array>
#include <cstdint>

namespace cl {

struct ResolvedStep {
    std::uint8_t velocity = 0; // 0 = silent; otherwise the exact velocity emitted
    std::uint8_t ratchet = 1;
    std::int8_t nudge = 0;
    bool flam = false;
    bool written = false; // a step exists here (lit, or silent through Density / mute)
    bool fromFill = false;
    Layer layer = Layer::groove;
};

struct ResolvedBar {
    std::array<std::array<ResolvedStep, kStepsPerBar>, kDrumLaneCount> lanes{};
};

struct ResolveInput {
    float density = 0.5f;    // 0..1
    float dynamics = 0.5f;   // 0..1
    bool fillActive = false; // this bar's fill window is live
};

// The Density value at or above which each stored step sounds: 0 for Core, (0, 0.5] for
// Groove, (0.5, 1] for Detail, and 2 for steps that are off. Rebuild when the pattern changes.
struct DensityMap {
    std::array<std::array<float, kMaxPatternSteps>, kDrumLaneCount> onAt{};
};

// True when hit A should arrive before hit B as Density rises within one layer.
bool rankBefore(int laneA, int stepA, int laneB, int stepB) noexcept CL_NONBLOCKING;

void buildDensityMap(const Pattern& pattern, DensityMap& out) noexcept CL_NONBLOCKING;

// v' = clamp(round(64 + (v - 64) * (0.5 + dynamics)), 1, 127)
std::uint8_t applyDynamics(int velocity, float dynamics) noexcept CL_NONBLOCKING;

void resolveBar(const Pattern& pattern, const DensityMap& map, int barInPattern, const ResolveInput& in,
                ResolvedBar& out) noexcept CL_NONBLOCKING;

// Convenience for callers that do not cache the map (UI, tests, export).
void resolveBar(const Pattern& pattern, int barInPattern, const ResolveInput& in,
                ResolvedBar& out) noexcept CL_NONBLOCKING;

} // namespace cl
```

- [ ] **Step 4: Write the implementation**

Create `engine/src/resolve.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/resolve.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cl {

namespace {

// Kick, snare, closed hat, open hat, clap, ghost, perc A, perc B.
constexpr std::array<int, kDrumLaneCount> kLanePriority{0, 1, 5, 4, 2, 3, 6, 7}; // indexed by lane

constexpr int metricWeight(int stepInBar) noexcept CL_NONBLOCKING {
    if (stepInBar == 0)
        return 0;
    if (stepInBar == 8)
        return 1;
    if (stepInBar == 4 || stepInBar == 12)
        return 2;
    return (stepInBar % 2) == 0 ? 3 : 4;
}

struct Ref {
    std::uint8_t lane = 0;
    std::uint8_t step = 0;
};

void rankLayer(const Pattern& pattern, Layer layer, float base, DensityMap& out) noexcept CL_NONBLOCKING {
    std::array<Ref, kDrumLaneCount * kMaxPatternSteps> refs{};
    int count = 0;
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const int loop = laneLoopSteps(pattern, lane);
        for (int step = 0; step < loop; ++step) {
            const Step& s = pattern.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
            if (s.on() && s.layer == layer)
                refs[static_cast<std::size_t>(count++)] = {static_cast<std::uint8_t>(lane),
                                                           static_cast<std::uint8_t>(step)};
        }
    }
    for (int i = 1; i < count; ++i) {
        const Ref key = refs[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && rankBefore(key.lane, key.step, refs[static_cast<std::size_t>(j)].lane,
                                    refs[static_cast<std::size_t>(j)].step)) {
            refs[static_cast<std::size_t>(j + 1)] = refs[static_cast<std::size_t>(j)];
            --j;
        }
        refs[static_cast<std::size_t>(j + 1)] = key;
    }
    for (int i = 0; i < count; ++i) {
        const Ref r = refs[static_cast<std::size_t>(i)];
        out.onAt[r.lane][r.step] = base + 0.5f * static_cast<float>(i + 1) / static_cast<float>(count);
    }
}

} // namespace

bool rankBefore(int laneA, int stepA, int laneB, int stepB) noexcept CL_NONBLOCKING {
    const int weightA = metricWeight(stepA % kStepsPerBar);
    const int weightB = metricWeight(stepB % kStepsPerBar);
    if (weightA != weightB)
        return weightA < weightB;
    const int priorityA = kLanePriority[static_cast<std::size_t>(laneA)];
    const int priorityB = kLanePriority[static_cast<std::size_t>(laneB)];
    if (priorityA != priorityB)
        return priorityA < priorityB;
    return stepA < stepB; // earlier bar first, then earlier step
}

void buildDensityMap(const Pattern& pattern, DensityMap& out) noexcept CL_NONBLOCKING {
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const int loop = laneLoopSteps(pattern, lane);
        for (int step = 0; step < kMaxPatternSteps; ++step) {
            const Step& s = pattern.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
            const bool core = step < loop && s.on() && s.layer == Layer::core;
            out.onAt[static_cast<std::size_t>(lane)][static_cast<std::size_t>(step)] = core ? 0.0f : 2.0f;
        }
    }
    rankLayer(pattern, Layer::groove, 0.0f, out);
    rankLayer(pattern, Layer::detail, 0.5f, out);
}

std::uint8_t applyDynamics(int velocity, float dynamics) noexcept CL_NONBLOCKING {
    const float d = std::clamp(dynamics, 0.0f, 1.0f);
    const float v = 64.0f + (static_cast<float>(velocity) - 64.0f) * (0.5f + d);
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 1, 127));
}

void resolveBar(const Pattern& pattern, const DensityMap& map, int barInPattern, const ResolveInput& in,
                ResolvedBar& out) noexcept CL_NONBLOCKING {
    const int bars = patternBars(pattern);
    const int bar = ((barInPattern % bars) + bars) % bars;
    const int fillFrom = in.fillActive ? fillWindowStart(pattern.fill) : kStepsPerBar;
    const float density = std::clamp(in.density, 0.0f, 1.0f) + 1.0e-6f;

    bool anySolo = false;
    for (const auto& lane : pattern.lanes)
        anySolo = anySolo || lane.solo;

    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const auto li = static_cast<std::size_t>(lane);
        const DrumLane& cfg = pattern.lanes[li];
        const bool audible = anySolo ? (cfg.solo && !cfg.muted) : !cfg.muted;
        // A lane takes the fill only if something is drawn for it inside the window.
        // A lane with nothing drawn there keeps playing the main pattern.
        bool laneFilled = false;
        for (int step = fillFrom; step < kStepsPerBar; ++step)
            laneFilled = laneFilled || pattern.fill.steps[li][static_cast<std::size_t>(step)].on();
        for (int step = 0; step < kStepsPerBar; ++step) {
            const auto si = static_cast<std::size_t>(step);
            ResolvedStep& r = out.lanes[li][si];
            r = {};
            const bool inFill = laneFilled && step >= fillFrom;
            const int stored = storedStep(pattern, lane, bar * kStepsPerBar + step);
            const Step& s = inFill ? pattern.fill.steps[li][si] : cfg.steps[static_cast<std::size_t>(stored)];
            if (!s.on())
                continue;
            const bool sounding = inFill || density >= map.onAt[li][static_cast<std::size_t>(stored)];
            r.written = true;
            r.fromFill = inFill;
            r.layer = s.layer;
            r.ratchet = static_cast<std::uint8_t>(std::clamp(static_cast<int>(s.ratchet), 1, 4));
            r.nudge = static_cast<std::int8_t>(std::clamp(static_cast<int>(s.nudge), -24, 24));
            r.flam = s.flam;
            r.velocity = (sounding && audible) ? applyDynamics(s.velocity, in.dynamics) : std::uint8_t{0};
        }
    }
}

void resolveBar(const Pattern& pattern, int barInPattern, const ResolveInput& in,
                ResolvedBar& out) noexcept CL_NONBLOCKING {
    DensityMap map;
    buildDensityMap(pattern, map);
    resolveBar(pattern, map, barInPattern, in, out);
}

} // namespace cl
```

In `engine/CMakeLists.txt`, add both files to the `cl_engine` source list. Put the header after `include/cl/phrase.h` and the source after `src/phrase.cpp`:

```cmake
    include/cl/resolve.h
```

```cmake
    src/resolve.cpp
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
cmake --build --preset engine --target cl-engine-tests && ./build/engine/tests/cl-engine-tests "[resolve]"
```

Expected: `All tests passed (` followed by an assertion count, `in 9 test cases)`.

- [ ] **Step 6: Commit**

```bash
git add engine/include/cl/resolve.h engine/src/resolve.cpp engine/CMakeLists.txt tests/engine/ResolveTests.cpp tests/CMakeLists.txt
git commit -m "feat(cutline): add resolveBar, the single source of truth for what sounds"
```

---

### Task 3: Timing and hits

**Files:**
- Create: `engine/include/cl/hits.h`
- Create: `tests/engine/HitsTests.cpp`
- Modify: `engine/CMakeLists.txt` (source list)
- Modify: `tests/CMakeLists.txt` (the `cl-engine-tests` source list)

**Interfaces:**
- Consumes: `Pattern`, `kStepsPerBar`, `kDrumLaneCount` (Task 1); `ResolvedBar`, `ResolvedStep`, `resolveBar` (Task 2).
- Produces: `kFlamMs` (20.0), `kFlamVelocityDrop` (40), `kFillEveryBars` (`{0, 2, 4, 8, 16}`), `double swingBeat(double beat, double swing)`, `double swungStepBeats(int step, double swing)`, `bool fillScheduled(std::int64_t absBar, int everyBars)`, `struct Hit { double beat; double durBeats; std::int8_t lane; std::int8_t step; std::uint8_t velocity; bool grace; }`, `struct HitTiming { double swing; double gateLen; double beatsPerMs; }`, and `template <typename Fn> void forEachHit(const Pattern&, const ResolvedBar&, double barOriginBeats, const HitTiming&, Fn&&)`.

`forEachHit` calls `fn` in lane order, then step order, with the flam's grace hit before the main hit. It does not clamp or sort: a hit may start before its bar's origin or at a negative beat. Plan 2's engine, MIDI export and truth test all go through it, so they cannot disagree about timing.

- [ ] **Step 1: Write the failing test**

Create `tests/engine/HitsTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/hits.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace cl;
using Catch::Approx;

namespace {

Step hit(int velocity) {
    Step s;
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = Layer::core;
    return s;
}

std::vector<Hit> hitsOf(const Pattern& p, double origin, const HitTiming& timing) {
    ResolvedBar bar;
    resolveBar(p, 0, {0.5f, 0.5f, false}, bar);
    std::vector<Hit> hits;
    forEachHit(p, bar, origin, timing, [&](const Hit& h) { hits.push_back(h); });
    return hits;
}

} // namespace

TEST_CASE("Swing delays odd 16ths, leaves even 16ths alone and never reorders time", "[engine][hits]") {
    CHECK(swungStepBeats(2, 1.0) == 0.5);
    CHECK(swungStepBeats(1, 0.0) == 0.25);
    CHECK(swungStepBeats(1, 1.0) == Approx(0.375));
    CHECK(swungStepBeats(16, 0.7) == 4.0);
    for (double swing : {0.0, 0.33, 0.67, 1.0}) {
        double last = -1.0;
        for (int i = 0; i <= 640; ++i) {
            const double warped = swingBeat(static_cast<double>(i) * 0.025, swing);
            REQUIRE(warped > last);
            last = warped;
        }
    }
}

TEST_CASE("A fill set to every N bars fires in bars N, 2N, 3N of a ruler that starts at 1", "[engine][hits]") {
    CHECK(fillScheduled(3, 4));
    CHECK(fillScheduled(7, 4));
    CHECK_FALSE(fillScheduled(4, 4));
    CHECK_FALSE(fillScheduled(0, 4));
    CHECK(fillScheduled(1, 2));
    CHECK_FALSE(fillScheduled(3, 0));
    CHECK(kFillEveryBars[0] == 0);
    CHECK(kFillEveryBars[4] == 16);
}

TEST_CASE("Ratchets divide the step evenly and a flam adds one early, quieter hit", "[engine][hits]") {
    Pattern p{};
    p.lanes[4].steps[14] = hit(96);
    p.lanes[4].steps[14].ratchet = 2;
    p.lanes[1].steps[4] = hit(118);
    p.lanes[1].steps[4].flam = true;

    const auto hits = hitsOf(p, 8.0, {0.0, 0.6, 172.0 / 60000.0});
    REQUIRE(hits.size() == 4);
    CHECK(hits[0].grace);
    CHECK(hits[0].lane == 1);
    CHECK(hits[0].velocity == 78);
    CHECK(hits[0].beat == Approx(9.0 - 20.0 * 172.0 / 60000.0));
    CHECK(hits[1].beat == 9.0);
    CHECK(hits[1].velocity == 118);
    CHECK_FALSE(hits[1].grace);
    CHECK(hits[2].beat == 11.5);
    CHECK(hits[3].beat == Approx(11.625));
    CHECK(hits[3].durBeats < 0.125);
}

TEST_CASE("Nudge and lane offset shift a hit without moving its step", "[engine][hits]") {
    Pattern p{};
    p.lanes[0].steps[4] = hit(96);
    p.lanes[0].steps[4].nudge = 12; // 12/96 = 1/8 beat late
    p.lanes[0].offsetMs = -10;
    const double beatsPerMs = 120.0 / 60000.0;
    const auto hits = hitsOf(p, 0.0, {0.0, 0.6, beatsPerMs});
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].step == 4);
    CHECK(hits[0].beat == Approx(1.0 + 0.125 - 10.0 * beatsPerMs));
}

TEST_CASE("Swung ratchets stay inside their step", "[engine][hits]") {
    Pattern p{};
    p.lanes[4].steps[1] = hit(96);
    p.lanes[4].steps[1].ratchet = 4;
    const auto hits = hitsOf(p, 0.0, {1.0, 0.6, 0.0});
    REQUIRE(hits.size() == 4);
    CHECK(hits[0].beat == Approx(0.375));
    CHECK(hits[3].beat < 0.5);
}

TEST_CASE("Early hits keep their lead at the start of the timeline and before it", "[engine][hits]") {
    Pattern p{};
    p.lanes[0].steps[0] = hit(96);
    p.lanes[0].steps[0].nudge = -12; // 1/8 beat early
    p.lanes[1].steps[0] = hit(118);
    p.lanes[1].steps[0].flam = true;
    const double beatsPerMs = 120.0 / 60000.0;

    for (double origin : {0.0, -4.0}) {
        const auto hits = hitsOf(p, origin, {0.0, 0.6, beatsPerMs});
        REQUIRE(hits.size() == 3);
        CHECK(hits[0].lane == 0);
        CHECK(hits[0].beat == Approx(origin - 0.125));
        CHECK(hits[1].grace);
        CHECK(hits[1].beat == Approx(origin - kFlamMs * beatsPerMs));
        CHECK(hits[2].beat == origin);
        CHECK(hits[1].beat < hits[2].beat);
    }
}
```

In `tests/CMakeLists.txt`, add the file to `cl-engine-tests`, directly after `engine/ResolveTests.cpp`:

```cmake
    engine/HitsTests.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset engine --target cl-engine-tests
```

Expected: the build fails with `fatal error: 'cl/hits.h' file not found`.

- [ ] **Step 3: Write the header**

Create `engine/include/cl/hits.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Turns a resolved bar into timed hits. Swing, ratchets, nudge, lane offset and flams are all
// applied here and nowhere else, so live playback, MIDI export and tests agree on timing.
#pragma once

#include "cl/groove.h"
#include "cl/nonblocking.h"
#include "cl/resolve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cl {

inline constexpr double kFlamMs = 20.0;
inline constexpr int kFlamVelocityDrop = 40;

// Bars between fills for each Fill choice: Off, 2, 4, 8, 16.
inline constexpr std::array<int, 5> kFillEveryBars{0, 2, 4, 8, 16};

// MPC-style 16th swing as a strictly increasing map of beat position. Even 16ths never move;
// odd 16ths are delayed by swing * 0.125 beats (0 = straight, 0.67 = triplet, 1 = dotted).
inline double swingBeat(double beat, double swing) noexcept CL_NONBLOCKING {
    const double s = std::clamp(swing, 0.0, 1.0);
    const double pair = std::floor(beat / 0.5);
    const double local = beat - pair * 0.5;
    const double mid = 0.25 + 0.125 * s;
    const double warped = local <= 0.25 ? (local / 0.25) * mid : mid + ((local - 0.25) / 0.25) * (0.5 - mid);
    return pair * 0.5 + warped;
}

// Start of `step` within its bar, in beats. Step 16 is the next bar's downbeat (4.0).
inline double swungStepBeats(int step, double swing) noexcept CL_NONBLOCKING {
    return swingBeat(static_cast<double>(step) * 0.25, swing);
}

// A fill set to every N bars fires in bars N, 2N, 3N... of a ruler that counts from 1.
constexpr bool fillScheduled(std::int64_t absBar, int everyBars) noexcept CL_NONBLOCKING {
    return everyBars > 0 && absBar >= 0 && ((absBar + 1) % everyBars) == 0;
}

struct Hit {
    double beat = 0.0;     // absolute start, in pattern beats; may precede its bar or be negative
    double durBeats = 0.1; // note length
    std::int8_t lane = 0;
    std::int8_t step = 0; // step in bar that produced it
    std::uint8_t velocity = 0;
    bool grace = false; // the flam's early hit
};

struct HitTiming {
    double swing = 0.0;      // 0..1
    double gateLen = 0.6;    // 0..1
    double beatsPerMs = 0.0; // pattern beats per millisecond at the current tempo and rate
};

// Calls fn(const Hit&) for every note-on of one resolved bar, lane by lane, step by step.
template <typename Fn>
void forEachHit(const Pattern& pattern, const ResolvedBar& bar, double barOriginBeats, const HitTiming& timing,
                Fn&& fn) {
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const auto li = static_cast<std::size_t>(lane);
        const double offsetBeats = static_cast<double>(pattern.lanes[li].offsetMs) * timing.beatsPerMs;
        const double baseDur = (lane == 5 || lane == 7) ? 0.36 : 0.12 + 0.14 * std::clamp(timing.gateLen, 0.0, 1.0);
        for (int step = 0; step < kStepsPerBar; ++step) {
            const ResolvedStep& r = bar.lanes[li][static_cast<std::size_t>(step)];
            if (r.velocity == 0)
                continue;
            const double start = barOriginBeats + swungStepBeats(step, timing.swing);
            const double next = barOriginBeats + swungStepBeats(step + 1, timing.swing);
            const int count = std::clamp(static_cast<int>(r.ratchet), 1, 4);
            const double interval = (next - start) / static_cast<double>(count);
            const double base = start + static_cast<double>(r.nudge) / 96.0 + offsetBeats;
            if (r.flam) {
                Hit grace;
                grace.beat = base - kFlamMs * timing.beatsPerMs;
                grace.durBeats = 0.08;
                grace.lane = static_cast<std::int8_t>(lane);
                grace.step = static_cast<std::int8_t>(step);
                grace.velocity =
                    static_cast<std::uint8_t>(std::max(1, static_cast<int>(r.velocity) - kFlamVelocityDrop));
                grace.grace = true;
                fn(grace);
            }
            for (int i = 0; i < count; ++i) {
                Hit hit;
                hit.beat = base + static_cast<double>(i) * interval;
                hit.durBeats = count > 1 ? std::min(baseDur, interval * 0.78) : baseDur;
                hit.lane = static_cast<std::int8_t>(lane);
                hit.step = static_cast<std::int8_t>(step);
                hit.velocity = r.velocity;
                fn(hit);
            }
        }
    }
}

} // namespace cl
```

In `engine/CMakeLists.txt`, add the header to the `cl_engine` source list, after `include/cl/engine.h`:

```cmake
    include/cl/hits.h
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build --preset engine --target cl-engine-tests && ./build/engine/tests/cl-engine-tests "[hits]"
```

Expected: `All tests passed (` followed by an assertion count, `in 6 test cases)`.

- [ ] **Step 5: Commit**

```bash
git add engine/include/cl/hits.h engine/CMakeLists.txt tests/engine/HitsTests.cpp tests/CMakeLists.txt
git commit -m "feat(cutline): add swing, fill schedule and hit timing"
```

---

### Task 4: Groove JSON

**Files:**
- Create: `core/include/cl/groove_json.h`
- Create: `core/src/groove_json_detail.h`
- Create: `core/src/groove_json.cpp`
- Create: `tests/core/GrooveJsonTests.cpp`
- Modify: `core/CMakeLists.txt` (source list)
- Modify: `tests/CMakeLists.txt:1` (the `cl-core-tests` source list)

**Interfaces:**
- Consumes: everything Task 1 produces; `hba::core::Json`, `hba::core::jsonStringOr`, `hba::core::jsonBoolOr` from `<hba/core/preset_json.h>`.
- Produces: `kGrooveFormat` (3), `kOldFormatError` (`"Cutline 2 preset: not supported in Cutline 3"`), `kVelocityAccent` (118), `kVelocityNormal` (96), `kVelocitySoft` (56), `std::optional<Pattern> parsePattern(std::string_view json, std::string* error = nullptr, std::string* name = nullptr)`, `std::string serialisePattern(const Pattern&, std::string_view name = {})`, `std::optional<GrooveBank> parseGrooveBank(std::string_view json, std::string* error = nullptr)`, `std::string serialiseGrooveBank(const GrooveBank&)`. In `cl::detail` (private to `cl_core`): `readPattern(const Json&, std::string*)`, `writePattern(const Pattern&)`, `readGrooveBank(const Json&, std::string*)`, `writeGrooveBank(const GrooveBank&)`.

Rules the code implements:

- A step string has 16 characters per bar. `|` and spaces are separators and are ignored. `X` is 118, `x` is 96, `o` is 56, `.` is off.
- A main-pattern string must be exactly `bars * 16` steps. A fill string must be exactly 16. A slicer string may be shorter than 32.
- A step set in two layer strings is an error.
- Only the fill window is part of the fill. Characters and edits before it are dropped on read and are not written.
- `edits` carries exact `velocity`, `ratchet`, `nudge` and `flam`. `"fill": true` targets the fill bar.
- The writer emits the nearest character and adds an edit only when the velocity is not exactly 118, 96 or 56, or the step has a ratchet, nudge or flam.
- The writer emits only the pattern's current length. Steps stored beyond it are not saved.
- Only a standalone pattern file is checked for `"format": 3`. A bank lives inside a preset, which carries the format itself (Plan 2).

- [ ] **Step 1: Write the failing test**

Create `tests/core/GrooveJsonTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove_json.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cl;

namespace {

Step hit(int velocity, Layer layer) {
    Step s;
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = layer;
    return s;
}

} // namespace

TEST_CASE("A pattern file reads step strings into layers and velocities", "[groove][json]") {
    const std::string json = R"({
        "format": 3, "name": "Two-Step", "bars": 1,
        "lanes": {
            "kick":  { "core": "X.........x.....", "groove": "..x.............", "detail": ".......o........" },
            "snare": { "core": "....X.......X..." },
            "hat":   { "core": "x.x.x.x.x.x.x.x.", "offsetMs": -1 }
        },
        "fill": { "beats": 2, "lanes": { "snare": "........x.x.x.xX" } },
        "edits": [ { "lane": "hat", "step": 14, "ratchet": 2 } ]
    })";
    std::string error;
    std::string name;
    const auto p = parsePattern(json, &error, &name);
    REQUIRE(p.has_value());
    CHECK(name == "Two-Step");
    CHECK(p->lanes[0].steps[0].velocity == kVelocityAccent);
    CHECK(p->lanes[0].steps[0].layer == Layer::core);
    CHECK(p->lanes[0].steps[2].layer == Layer::groove);
    CHECK(p->lanes[0].steps[7].velocity == kVelocitySoft);
    CHECK(p->lanes[0].steps[7].layer == Layer::detail);
    CHECK(p->lanes[4].offsetMs == -1);
    CHECK(p->lanes[4].steps[14].ratchet == 2);
    CHECK(p->fill.beats == 2);
    CHECK(p->fill.steps[1][15].velocity == kVelocityAccent);
    CHECK_FALSE(p->fill.steps[1][0].on());
}

TEST_CASE("Patterns and banks round-trip with every field set", "[groove][json]") {
    Pattern p = defaultBeat();
    p.bars = 2;
    p.lanes[4].steps[14].ratchet = 3;
    p.lanes[1].steps[20] = hit(73, Layer::detail);
    p.lanes[1].steps[20].nudge = -6;
    p.lanes[6].length = 7;
    p.lanes[6].offsetMs = -3;
    p.lanes[2].midiNote = 40;
    p.lanes[3].muted = true;
    p.lanes[5].solo = true;
    p.fill.beats = 1;
    p.fill.steps[1][15] = hit(118, Layer::groove);
    p.fill.steps[1][15].flam = true;

    std::string error;
    const auto back = parsePattern(serialisePattern(p, "Round Trip"), &error);
    REQUIRE(back.has_value());
    CHECK(*back == p);

    GrooveBank bank;
    bank.scenes[2] = p;
    bank.slicerLength = 12;
    bank.slicerSteps[3] = hit(96, Layer::groove);
    bank.slicerSteps[3].ratchet = 2;
    const auto bankBack = parseGrooveBank(serialiseGrooveBank(bank), &error);
    REQUIRE(bankBack.has_value());
    CHECK(*bankBack == bank);
}

TEST_CASE("Bad pattern files are rejected with a reason", "[groove][json]") {
    std::string error;

    CHECK_FALSE(parsePattern(R"({"bars":1})", &error));
    CHECK(error == kOldFormatError);
    CHECK_FALSE(parsePattern(R"({"format":2,"bars":1})", &error));
    CHECK(error == kOldFormatError);

    CHECK_FALSE(parsePattern(R"({"format":3,"lanes":{"kick":{"core":"x...............","groove":"x..............."}}})",
                             &error));
    CHECK(error.find("more than one layer") != std::string::npos);

    CHECK_FALSE(parsePattern(R"({"format":3,"lanes":{"kick":{"core":"x..."}}})", &error));
    CHECK(error.find("expected 16") != std::string::npos);

    CHECK_FALSE(parsePattern(R"({"format":3,"bars":3})", &error));
    CHECK_FALSE(parsePattern(R"({"format":3,"lanes":{"cowbell":{"core":"x..............."}}})", &error));
    CHECK_FALSE(parsePattern(R"({"format":3,"lanes":{"kick":{"core":"x......?........"}}})", &error));
    CHECK_FALSE(parsePattern("not json", &error));
}

TEST_CASE("Fill steps before the fill window are ignored on read and not written", "[groove][json]") {
    std::string error;
    const auto p = parsePattern(R"({
        "format": 3,
        "fill": { "beats": 1, "lanes": { "snare": "x.......x.....xX" } },
        "edits": [ { "lane": "snare", "step": 2, "fill": true, "velocity": 100 } ]
    })",
                                &error);
    REQUIRE(p.has_value());
    for (std::size_t step = 0; step < 12; ++step)
        CHECK_FALSE(p->fill.steps[1][step].on());
    CHECK(p->fill.steps[1][14].on());
    CHECK(p->fill.steps[1][15].velocity == kVelocityAccent);

    Pattern drawn{};
    drawn.fill.beats = 2;
    drawn.fill.steps[0][4] = hit(96, Layer::groove); // before the 2-beat window
    drawn.fill.steps[0][8] = hit(96, Layer::groove); // inside it
    const auto back = parsePattern(serialisePattern(drawn), &error);
    REQUIRE(back.has_value());
    CHECK_FALSE(back->fill.steps[0][4].on());
    CHECK(back->fill.steps[0][8].on());
}
```

In `tests/CMakeLists.txt`, change line 1 to:

```cmake
add_executable(cl-core-tests core/CoreTests.cpp core/GrooveTests.cpp core/GrooveJsonTests.cpp)
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset engine --target cl-core-tests
```

Expected: the build fails with `fatal error: 'cl/groove_json.h' file not found`.

- [ ] **Step 3: Write the public header**

Create `core/include/cl/groove_json.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// JSON form of the Cutline 3 groove model. One form serves factory pattern files, presets
// and plugin state: step strings per layer, plus an "edits" list for what a character cannot say.
#pragma once

#include "cl/groove.h"

#include <optional>
#include <string>
#include <string_view>

namespace cl {

inline constexpr int kGrooveFormat = 3;
inline constexpr std::string_view kOldFormatError = "Cutline 2 preset: not supported in Cutline 3";

// Step characters: accent, normal, soft.
inline constexpr int kVelocityAccent = 118; // 'X'
inline constexpr int kVelocityNormal = 96;  // 'x'
inline constexpr int kVelocitySoft = 56;    // 'o'

// A standalone pattern file. Requires "format": 3 at the top level.
std::optional<Pattern> parsePattern(std::string_view json, std::string* error = nullptr, std::string* name = nullptr);
std::string serialisePattern(const Pattern& pattern, std::string_view name = {});

// The bank object stored inside presets and plugin state: {"scenes": [...], "slicer": {...}}.
std::optional<GrooveBank> parseGrooveBank(std::string_view json, std::string* error = nullptr);
std::string serialiseGrooveBank(const GrooveBank& bank);

} // namespace cl
```

- [ ] **Step 4: Write the detail header**

Create `core/src/groove_json_detail.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Json-level groove codec, shared inside cl_core (preset.cpp embeds a bank in a preset).
#pragma once

#include "cl/groove.h"

#include <hba/core/preset_json.h>

#include <optional>
#include <string>

namespace cl::detail {

using Json = hba::core::Json;

std::optional<Pattern> readPattern(const Json& value, std::string* error);
Json writePattern(const Pattern& pattern);

std::optional<GrooveBank> readGrooveBank(const Json& value, std::string* error);
Json writeGrooveBank(const GrooveBank& bank);

} // namespace cl::detail
```

- [ ] **Step 5: Write the implementation**

Create `core/src/groove_json.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/groove_json.h"

#include "groove_json_detail.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace cl {

namespace detail {

namespace {

constexpr std::array<std::string_view, 3> kLayerKeys{"core", "groove", "detail"};

bool fail(std::string* error, std::string message) {
    if (error != nullptr)
        *error = std::move(message);
    return false;
}

int laneOfKey(std::string_view key) {
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        if (kLaneKeys[static_cast<std::size_t>(lane)] == key)
            return lane;
    }
    return -1;
}

int velocityOfChar(char c) {
    switch (c) {
    case 'X':
        return kVelocityAccent;
    case 'x':
        return kVelocityNormal;
    case 'o':
        return kVelocitySoft;
    case '.':
        return 0;
    default:
        return -1;
    }
}

char charOfVelocity(int velocity) {
    const int dAccent = std::abs(velocity - kVelocityAccent);
    const int dNormal = std::abs(velocity - kVelocityNormal);
    const int dSoft = std::abs(velocity - kVelocitySoft);
    if (dNormal <= dAccent && dNormal <= dSoft)
        return 'x';
    return dAccent < dSoft ? 'X' : 'o';
}

bool exactChar(int velocity) {
    return velocity == kVelocityAccent || velocity == kVelocityNormal || velocity == kVelocitySoft;
}

// Reads a step string ('|' and spaces are separators) into `count` steps of one layer.
bool readSteps(const Json& value, Step* steps, int count, bool exactLength, bool setLayer, Layer layer,
               const std::string& where, std::string* error) {
    if (!value.is_string())
        return fail(error, where + " must be a string");
    std::string text;
    for (const char c : value.get<std::string>()) {
        if (c != '|' && c != ' ')
            text.push_back(c);
    }
    const int length = static_cast<int>(text.size());
    if (exactLength ? length != count : length > count)
        return fail(error, where + " has " + std::to_string(length) + " steps, expected " + std::to_string(count));
    for (int i = 0; i < length; ++i) {
        const int velocity = velocityOfChar(text[static_cast<std::size_t>(i)]);
        if (velocity < 0)
            return fail(error, where + " has an unknown step character");
        if (velocity == 0)
            continue;
        Step& s = steps[i];
        if (s.on())
            return fail(error, where + " sets step " + std::to_string(i) + " in more than one layer");
        s.velocity = static_cast<std::uint8_t>(velocity);
        if (setLayer)
            s.layer = layer;
    }
    return true;
}

bool intField(const Json& obj, const char* key, int lo, int hi, int& out) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer())
        return false;
    out = std::clamp(it->get<int>(), lo, hi);
    return true;
}

void applyEdit(const Json& edit, Step& s) {
    int v = 0;
    if (intField(edit, "velocity", 1, 127, v))
        s.velocity = static_cast<std::uint8_t>(v);
    if (intField(edit, "ratchet", 1, 4, v))
        s.ratchet = static_cast<std::uint8_t>(v);
    if (intField(edit, "nudge", -24, 24, v))
        s.nudge = static_cast<std::int8_t>(v);
    s.flam = hba::core::jsonBoolOr(edit, "flam", s.flam);
}

bool needsEdit(const Step& s) { return s.on() && (!exactChar(s.velocity) || s.ratchet != 1 || s.nudge != 0 || s.flam); }

Json editFor(const Step& s) {
    Json e = Json::object();
    if (!exactChar(s.velocity))
        e["velocity"] = static_cast<int>(s.velocity);
    if (s.ratchet != 1)
        e["ratchet"] = static_cast<int>(s.ratchet);
    if (s.nudge != 0)
        e["nudge"] = static_cast<int>(s.nudge);
    if (s.flam)
        e["flam"] = true;
    return e;
}

// Writes `count` steps of one layer (or of every layer when `anyLayer`), 16 per bar.
std::string stepString(const Step* steps, int count, bool anyLayer, Layer layer, bool& any) {
    std::string text;
    any = false;
    for (int i = 0; i < count; ++i) {
        if (i > 0 && (i % kStepsPerBar) == 0)
            text.push_back('|');
        const Step& s = steps[i];
        const bool show = s.on() && (anyLayer || s.layer == layer);
        text.push_back(show ? charOfVelocity(s.velocity) : '.');
        any = any || show;
    }
    return text;
}

} // namespace

std::optional<Pattern> readPattern(const Json& value, std::string* error) {
    if (!value.is_object()) {
        fail(error, "pattern is not a JSON object");
        return std::nullopt;
    }
    Pattern p{};
    int bars = 1;
    if (value.contains("bars")) {
        if (!value["bars"].is_number_integer() ||
            (value["bars"].get<int>() != 1 && value["bars"].get<int>() != 2 && value["bars"].get<int>() != 4)) {
            fail(error, "\"bars\" must be 1, 2 or 4");
            return std::nullopt;
        }
        bars = value["bars"].get<int>();
    }
    p.bars = static_cast<std::uint8_t>(bars);
    const int steps = bars * kStepsPerBar;

    if (value.contains("lanes")) {
        if (!value["lanes"].is_object()) {
            fail(error, "\"lanes\" must be an object");
            return std::nullopt;
        }
        for (const auto& [key, lo] : value["lanes"].items()) {
            const int lane = laneOfKey(key);
            if (lane < 0 || !lo.is_object()) {
                fail(error, "unknown lane \"" + key + "\"");
                return std::nullopt;
            }
            DrumLane& dl = p.lanes[static_cast<std::size_t>(lane)];
            for (std::size_t layer = 0; layer < kLayerKeys.size(); ++layer) {
                const std::string layerKey(kLayerKeys[layer]);
                if (lo.contains(layerKey) && !readSteps(lo[layerKey], dl.steps.data(), steps, true, true,
                                                        static_cast<Layer>(layer), key + "." + layerKey, error))
                    return std::nullopt;
            }
            int v = 0;
            if (intField(lo, "length", 0, kMaxPatternSteps, v))
                dl.length = static_cast<std::uint8_t>(v);
            if (intField(lo, "midiNote", 0, 127, v))
                dl.midiNote = static_cast<std::uint8_t>(v);
            if (intField(lo, "offsetMs", -20, 20, v))
                dl.offsetMs = static_cast<std::int8_t>(v);
            dl.muted = hba::core::jsonBoolOr(lo, "muted");
            dl.solo = hba::core::jsonBoolOr(lo, "solo");
        }
    }

    if (value.contains("fill")) {
        const Json& fo = value["fill"];
        if (!fo.is_object()) {
            fail(error, "\"fill\" must be an object");
            return std::nullopt;
        }
        int beats = 2;
        if (fo.contains("beats")) {
            if (!fo["beats"].is_number_integer() ||
                (fo["beats"].get<int>() != 1 && fo["beats"].get<int>() != 2 && fo["beats"].get<int>() != 4)) {
                fail(error, "fill \"beats\" must be 1, 2 or 4");
                return std::nullopt;
            }
            beats = fo["beats"].get<int>();
        }
        p.fill.beats = static_cast<std::uint8_t>(beats);
        if (fo.contains("lanes") && fo["lanes"].is_object()) {
            for (const auto& [key, text] : fo["lanes"].items()) {
                const int lane = laneOfKey(key);
                if (lane < 0) {
                    fail(error, "unknown fill lane \"" + key + "\"");
                    return std::nullopt;
                }
                if (!readSteps(text, p.fill.steps[static_cast<std::size_t>(lane)].data(), kStepsPerBar, true, false,
                               Layer::groove, "fill." + key, error))
                    return std::nullopt;
            }
        }
    }

    if (value.contains("edits") && value["edits"].is_array()) {
        for (const auto& edit : value["edits"]) {
            if (!edit.is_object())
                continue;
            const int lane = laneOfKey(hba::core::jsonStringOr(edit, "lane"));
            const bool inFill = hba::core::jsonBoolOr(edit, "fill");
            int step = 0;
            if (lane < 0 || !intField(edit, "step", 0, (inFill ? kStepsPerBar : steps) - 1, step)) {
                fail(error, "edit needs a known \"lane\" and a \"step\"");
                return std::nullopt;
            }
            const auto li = static_cast<std::size_t>(lane);
            const auto si = static_cast<std::size_t>(step);
            applyEdit(edit, inFill ? p.fill.steps[li][si] : p.lanes[li].steps[si]);
        }
    }
    // Only the fill window is part of the fill: characters and edits before it are ignored.
    const auto fillFrom = static_cast<std::size_t>(fillWindowStart(p.fill));
    for (auto& row : p.fill.steps) {
        for (std::size_t step = 0; step < fillFrom; ++step)
            row[step] = {};
    }
    return p;
}

Json writePattern(const Pattern& pattern) {
    Json obj = Json::object();
    obj["bars"] = patternBars(pattern);
    const int steps = patternSteps(pattern);
    Json edits = Json::array();

    Json lanes = Json::object();
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        const DrumLane& dl = pattern.lanes[static_cast<std::size_t>(lane)];
        const std::string key(kLaneKeys[static_cast<std::size_t>(lane)]);
        Json lo = Json::object();
        for (std::size_t layer = 0; layer < kLayerKeys.size(); ++layer) {
            bool any = false;
            std::string text = stepString(dl.steps.data(), steps, false, static_cast<Layer>(layer), any);
            if (any)
                lo[std::string(kLayerKeys[layer])] = std::move(text);
        }
        if (dl.length != 0)
            lo["length"] = static_cast<int>(dl.length);
        if (dl.midiNote != 0)
            lo["midiNote"] = static_cast<int>(dl.midiNote);
        if (dl.offsetMs != 0)
            lo["offsetMs"] = static_cast<int>(dl.offsetMs);
        if (dl.muted)
            lo["muted"] = true;
        if (dl.solo)
            lo["solo"] = true;
        if (!lo.empty())
            lanes[key] = std::move(lo);
        for (int step = 0; step < steps; ++step) {
            const Step& s = dl.steps[static_cast<std::size_t>(step)];
            if (!needsEdit(s))
                continue;
            Json e = editFor(s);
            e["lane"] = key;
            e["step"] = step;
            edits.push_back(std::move(e));
        }
    }
    obj["lanes"] = std::move(lanes);

    Json fill = Json::object();
    fill["beats"] = kStepsPerBar / 4 - fillWindowStart(pattern.fill) / 4;
    Json fillLanes = Json::object();
    for (int lane = 0; lane < kDrumLaneCount; ++lane) {
        auto row = pattern.fill.steps[static_cast<std::size_t>(lane)];
        for (int step = 0; step < fillWindowStart(pattern.fill); ++step)
            row[static_cast<std::size_t>(step)] = {};
        const std::string key(kLaneKeys[static_cast<std::size_t>(lane)]);
        bool any = false;
        std::string text = stepString(row.data(), kStepsPerBar, true, Layer::groove, any);
        if (any)
            fillLanes[key] = std::move(text);
        for (int step = 0; step < kStepsPerBar; ++step) {
            const Step& s = row[static_cast<std::size_t>(step)];
            if (!needsEdit(s))
                continue;
            Json e = editFor(s);
            e["lane"] = key;
            e["step"] = step;
            e["fill"] = true;
            edits.push_back(std::move(e));
        }
    }
    fill["lanes"] = std::move(fillLanes);
    obj["fill"] = std::move(fill);

    if (!edits.empty())
        obj["edits"] = std::move(edits);
    return obj;
}

std::optional<GrooveBank> readGrooveBank(const Json& value, std::string* error) {
    if (!value.is_object()) {
        fail(error, "bank is not a JSON object");
        return std::nullopt;
    }
    GrooveBank bank{};
    bank.scenes = {};
    if (value.contains("scenes")) {
        if (!value["scenes"].is_array()) {
            fail(error, "\"scenes\" must be an array");
            return std::nullopt;
        }
        const auto& arr = value["scenes"];
        for (std::size_t i = 0; i < arr.size() && i < bank.scenes.size(); ++i) {
            const auto pattern = readPattern(arr[i], error);
            if (!pattern)
                return std::nullopt;
            bank.scenes[i] = *pattern;
        }
    }
    if (value.contains("slicer") && value["slicer"].is_object()) {
        const Json& so = value["slicer"];
        int v = 0;
        if (intField(so, "length", 1, kMaxSlicerSteps, v))
            bank.slicerLength = static_cast<std::uint8_t>(v);
        if (so.contains("steps") && !readSteps(so["steps"], bank.slicerSteps.data(), kMaxSlicerSteps, false, false,
                                               Layer::groove, "slicer.steps", error))
            return std::nullopt;
        if (so.contains("edits") && so["edits"].is_array()) {
            for (const auto& edit : so["edits"]) {
                int step = 0;
                if (edit.is_object() && intField(edit, "step", 0, kMaxSlicerSteps - 1, step))
                    applyEdit(edit, bank.slicerSteps[static_cast<std::size_t>(step)]);
            }
        }
    }
    return bank;
}

Json writeGrooveBank(const GrooveBank& bank) {
    Json obj = Json::object();
    Json scenes = Json::array();
    for (const auto& pattern : bank.scenes)
        scenes.push_back(writePattern(pattern));
    obj["scenes"] = std::move(scenes);

    Json slicer = Json::object();
    const int length = std::clamp(static_cast<int>(bank.slicerLength), 1, kMaxSlicerSteps);
    slicer["length"] = length;
    bool any = false;
    slicer["steps"] = stepString(bank.slicerSteps.data(), length, true, Layer::groove, any);
    Json edits = Json::array();
    for (int step = 0; step < length; ++step) {
        const Step& s = bank.slicerSteps[static_cast<std::size_t>(step)];
        if (!needsEdit(s))
            continue;
        Json e = editFor(s);
        e["step"] = step;
        edits.push_back(std::move(e));
    }
    if (!edits.empty())
        slicer["edits"] = std::move(edits);
    obj["slicer"] = std::move(slicer);
    return obj;
}

} // namespace detail

std::optional<Pattern> parsePattern(std::string_view json, std::string* error, std::string* name) {
    const detail::Json root = detail::Json::parse(json, nullptr, false);
    if (!root.is_object()) {
        if (error != nullptr)
            *error = "pattern is not a JSON object";
        return std::nullopt;
    }
    const auto format = root.find("format");
    if (format == root.end() || !format->is_number_integer() || format->get<int>() < kGrooveFormat) {
        if (error != nullptr)
            *error = std::string(kOldFormatError);
        return std::nullopt;
    }
    if (name != nullptr)
        *name = hba::core::jsonStringOr(root, "name");
    return detail::readPattern(root, error);
}

std::string serialisePattern(const Pattern& pattern, std::string_view name) {
    detail::Json root = detail::Json::object();
    root["format"] = kGrooveFormat;
    if (!name.empty())
        root["name"] = std::string(name);
    root.update(detail::writePattern(pattern));
    return root.dump(2) + "\n";
}

std::optional<GrooveBank> parseGrooveBank(std::string_view json, std::string* error) {
    return detail::readGrooveBank(detail::Json::parse(json, nullptr, false), error);
}

std::string serialiseGrooveBank(const GrooveBank& bank) { return detail::writeGrooveBank(bank).dump(); }

} // namespace cl
```

In `core/CMakeLists.txt`, add the three files to the `cl_core` source list. Put the header after `include/cl/groove.h` and the sources after `src/params.cpp`:

```cmake
    include/cl/groove_json.h
```

```cmake
    src/groove_json.cpp
    src/groove_json_detail.h
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
cmake --build --preset engine --target cl-core-tests && ./build/engine/tests/cl-core-tests "[json]"
```

Expected: `All tests passed (` followed by an assertion count, `in 4 test cases)`.

- [ ] **Step 7: Commit**

```bash
git add core/include/cl/groove_json.h core/src/groove_json.cpp core/src/groove_json_detail.h core/CMakeLists.txt tests/core/GrooveJsonTests.cpp tests/CMakeLists.txt
git commit -m "feat(cutline): read and write groove patterns and banks as JSON"
```

---

### Task 5: Seed patterns from the 2.0 style tables

**Files:**
- Create: `tools/seed_patterns.py`
- Create: `patterns/amen.json`, `patterns/think.json`, `patterns/neuro.json`, `patterns/halftime.json`, `patterns/ukg.json`, `patterns/footwork.json`, `patterns/stepper.json`, `patterns/electro.json`, `patterns/drill.json`, `patterns/afro.json`, `patterns/trap.json` (generated)

**Interfaces:**
- Consumes: `engine/src/styles.cpp` as text. It does not compile or link anything.
- Produces: 11 pattern files in the JSON form Task 4 reads. Each has `"format": 3`, `"bars": 2`, layered lanes and one fill.

What the converter does, so the output can be reviewed:

1. Reads each style's `coreMask`, `grooveMask`, `spiceMask`, `accentMask`, `defaultLen` and `pocketMs` with a regular expression. It evaluates no code.
2. Maps `coreMask` to Core, else `grooveMask` to Groove, else `spiceMask` to Detail. Accented hits become `X`, others `x`, every ghost-lane hit `o`. `pocketMs` rounds to `offsetMs`. A `defaultLen` under 16 becomes the lane's `length`. Euclidean Lab, the twelfth style, is computed in 2.0 and is skipped.
3. Demotes hits until the pattern meets the spec's lint. Snare Core keeps only hits present at the same step in both bars. Then, per bar, the lowest-ranked hit is demoted one layer until Core has at most 16 note-ons, Groove has at most 3 snare, ghost or clap note-ons, and Core plus Groove has at most 24. A full-length lane demotes the same step in the other bar too, so bars stay alike. The kick on beat 1 and the snare are never demoted out of Core.
4. Attaches one of four drawn fills by style.
5. Asserts the lint itself before writing each file.

- [ ] **Step 1: Write the converter**

Create `tools/seed_patterns.py`:

```python
#!/usr/bin/env python3
"""One-off: convert Cutline 2.0 style tables (engine/src/styles.cpp) into Cutline 3 pattern files.

Usage: python3 tools/seed_patterns.py engine/src/styles.cpp patterns
Deleted together with styles.cpp once the engine no longer uses it.
"""
import json
import re
import sys
from pathlib import Path

NAMES = ["Amen", "Think", "Neuro", "Halftime", "UKG", "Footwork", "Stepper", "Electro", "Drill", "Afro", "Trap"]
LANE_KEYS = ["kick", "snare", "ghost", "clap", "hat", "openHat", "percA", "percB"]
LANE_PRIORITY = [0, 1, 4, 5, 3, 2, 6, 7]  # kick, snare, closed hat, open hat, clap, ghost, perc A, perc B
SNARE_FAMILY = (1, 2, 3)
CORE, GROOVE, DETAIL = 0, 1, 2
LAYER_KEYS = ["core", "groove", "detail"]
BARS = 2
STEPS = BARS * 16

# One drawn fill per seed: (beats, {lane key: 16-character string}).
FILL_SNARE = (2, {"kick": "........x.......", "snare": "........x.x.x.xX"})
FILL_TOMS = (2, {"percB": "........xx......", "percA": "..........xx....", "snare": "............xx..", "kick": "..............x."})
FILL_PICKUP = (1, {"kick": "............x...", "snare": "..............xX"})
FILL_DROP = (2, {"kick": "........X.......", "snare": "............X...", "clap": "............x...", "openHat": "..............x."})
FILLS = {"Amen": FILL_SNARE, "Think": FILL_SNARE, "Neuro": FILL_PICKUP, "Halftime": FILL_DROP,
         "UKG": FILL_PICKUP, "Footwork": FILL_TOMS, "Stepper": FILL_PICKUP, "Electro": FILL_TOMS,
         "Drill": FILL_SNARE, "Afro": FILL_TOMS, "Trap": FILL_DROP}


def m32(steps):
    mask = 0
    for s in steps:
        if 0 <= s < 32:
            mask |= 1 << s
    return mask


def dup16(steps):
    bar = m32(steps)
    return (bar | (bar << 16)) & 0xFFFFFFFF


MASK_RE = re.compile(r"(m32|dup16)\(\{([^}]*)\}\)")


def parse_masks(text):
    """Reads a C++ initialiser list of m32({...}) / dup16({...}) calls without evaluating any code."""
    out = []
    for fn, body in MASK_RE.findall(text):
        steps = [int(v) for v in body.split(",") if v.strip()]
        out.append(dup16(steps) if fn == "dup16" else m32(steps))
    return out


STYLE_RE = re.compile(
    r"\.coreMask\s*=\s*\{(?P<core>.*?)\},\s*"
    r"\.grooveMask\s*=\s*\{(?P<groove>.*?)\},\s*"
    r"\.spiceMask\s*=\s*\{(?P<spice>.*?)\},\s*"
    r"\.accentMask\s*=\s*\{(?P<accent>.*?)\},\s*"
    r"\.defaultLen\s*=\s*\{(?P<len>.*?)\},.*?"
    r"\.pocketMs\s*=\s*\{(?P<pocket>.*?)\},",
    re.S)


def parse_styles(source):
    styles = []
    for m in STYLE_RE.finditer(source):
        def masks(key):
            values = parse_masks(m.group(key))
            return values + [0] * (8 - len(values))
        styles.append({
            "core": masks("core"), "groove": masks("groove"), "spice": masks("spice"), "accent": masks("accent"),
            "len": [int(v) for v in m.group("len").split(",")],
            "pocket": [float(v) for v in m.group("pocket").split(",")],
        })
    return styles


def metric_weight(step_in_bar):
    if step_in_bar == 0:
        return 0
    if step_in_bar == 8:
        return 1
    if step_in_bar in (4, 12):
        return 2
    return 3 if step_in_bar % 2 == 0 else 4


def rank_key(lane, step):
    return (metric_weight(step % 16), LANE_PRIORITY.index(lane), step // 16, step)


def build(style):
    """Returns (cells, lengths): cells[lane][stored step] = [layer, velocity char]; lengths[lane] = 0 or loop length."""
    cells = [dict() for _ in range(8)]
    lengths = [0] * 8
    for lane in range(8):
        length = style["len"][lane]
        stored = STEPS if length >= 16 else length
        if length < 16:
            lengths[lane] = length

        def bit(mask, step):
            if length >= 32:
                return bool(mask >> step & 1)
            if length == 16:
                return bool(mask >> (step % 16) & 1)
            return any(mask >> b & 1 and b % length == step for b in range(32))

        for step in range(stored):
            if bit(style["core"][lane], step):
                layer = CORE
            elif bit(style["groove"][lane], step):
                layer = GROOVE
            elif bit(style["spice"][lane], step):
                layer = DETAIL
            else:
                continue
            char = "o" if lane == 2 else ("X" if bit(style["accent"][lane], step) else "x")
            cells[lane][step] = [layer, char]
    return cells, lengths


def stored_step(lengths, lane, step):
    return step % lengths[lane] if lengths[lane] else step


def bar_hits(cells, lengths, bar, layers, lanes=range(8)):
    """Stored (lane, step) pairs sounding in `bar` for the given layers, one entry per sounding pattern step."""
    out = []
    for lane in lanes:
        for step in range(bar * 16, bar * 16 + 16):
            src = stored_step(lengths, lane, step)
            cell = cells[lane].get(src)
            if cell and cell[0] in layers:
                out.append((lane, src))
    return out


def curate(cells, lengths):
    # Anchor: snare Core must be the same in every bar.
    for step, cell in list(cells[1].items()):
        if cell[0] == CORE:
            twin = cells[1].get((step + 16) % STEPS)
            if not twin or twin[0] != CORE:
                cell[0] = GROOVE

    def demote(bar, layers, lanes, limit, to, protect=lambda lane, step: False):
        while True:
            hits = bar_hits(cells, lengths, bar, layers, lanes)
            if len(hits) <= limit:
                return
            movable = [h for h in set(hits) if cells[h[0]][h[1]][0] == to - 1 and not protect(*h)]
            if not movable:
                raise SystemExit(f"cannot meet budget {limit} in bar {bar}")
            lane, step = max(movable, key=lambda h: rank_key(*h))
            cells[lane][step][0] = to
            # Keep bars alike: a full-length lane demotes the same step in the other bar too.
            twin = cells[lane].get((step + 16) % STEPS) if not lengths[lane] else None
            if twin and twin[0] == to - 1 and not protect(lane, (step + 16) % STEPS):
                twin[0] = to

    for bar in range(BARS):
        demote(bar, (CORE,), range(8), 16, GROOVE,
               protect=lambda lane, step: lane == 1 or (lane == 0 and step % 16 == 0))
    for bar in range(BARS):
        demote(bar, (GROOVE,), SNARE_FAMILY, 3, DETAIL)
    for bar in range(BARS):
        demote(bar, (CORE, GROOVE), range(8), 24, DETAIL)


def lint(name, cells, lengths):
    assert all(cells[0].get(b * 16, [None])[0] == CORE for b in range(BARS)), f"{name}: kick anchor"
    snare_core = [sorted(s % 16 for s, c in cells[1].items() if c[0] == CORE and s // 16 == b) for b in range(BARS)]
    assert snare_core[0] and all(sc == snare_core[0] for sc in snare_core), f"{name}: snare anchor {snare_core}"
    rows = []
    for bar in range(BARS):
        core = len(bar_hits(cells, lengths, bar, (CORE,)))
        half = len(bar_hits(cells, lengths, bar, (CORE, GROOVE)))
        fam = len(bar_hits(cells, lengths, bar, (GROOVE,), SNARE_FAMILY))
        assert core <= 16 and half <= 24 and fam <= 3, f"{name} bar {bar}: {core}/{half}/{fam}"
        rows.append((core, half, fam))
    return rows


def lane_strings(cells, lane, layer):
    chars = ["."] * STEPS
    for step, (cell_layer, char) in cells[lane].items():
        if cell_layer == layer:
            chars[step] = char
    text = "".join(chars)
    return None if set(text) == {"."} else text[:16] + "|" + text[16:]


def to_json(name, style, cells, lengths):
    lanes = {}
    for lane, key in enumerate(LANE_KEYS):
        entry = {}
        for layer, layer_key in enumerate(LAYER_KEYS):
            text = lane_strings(cells, lane, layer)
            if text:
                entry[layer_key] = text
        if not entry:
            continue
        if lengths[lane]:
            entry["length"] = lengths[lane]
        offset = int(round(style["pocket"][lane]))
        if offset:
            entry["offsetMs"] = offset
        lanes[key] = entry
    beats, fill_lanes = FILLS[name]
    return {"format": 3, "name": name, "bars": BARS, "lanes": lanes, "fill": {"beats": beats, "lanes": fill_lanes}}


def main():
    source = Path(sys.argv[1]).read_text()
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    styles = parse_styles(source)
    assert len(styles) >= len(NAMES), f"parsed {len(styles)} styles"
    for name, style in zip(NAMES, styles):
        cells, lengths = build(style)
        curate(cells, lengths)
        rows = lint(name, cells, lengths)
        path = out_dir / (name.lower() + ".json")
        path.write_text(json.dumps(to_json(name, style, cells, lengths), indent=2) + "\n")
        print(f"{name:9} core/50%/snare-family per bar: {rows}  -> {path}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it**

```bash
python3 tools/seed_patterns.py engine/src/styles.cpp patterns
```

Expected: 11 lines, one per style, and no assertion error. The numbers are Core, 50% Density and non-Core snare-family note-ons for bar 1 then bar 2:

```
Amen      core/50%/snare-family per bar: [(16, 19, 2), (14, 19, 3)]  -> patterns/amen.json
Think     core/50%/snare-family per bar: [(16, 24, 1), (15, 24, 1)]  -> patterns/think.json
Neuro     core/50%/snare-family per bar: [(16, 22, 3), (15, 23, 3)]  -> patterns/neuro.json
Halftime  core/50%/snare-family per bar: [(16, 22, 1), (14, 23, 2)]  -> patterns/halftime.json
UKG       core/50%/snare-family per bar: [(16, 24, 1), (15, 23, 1)]  -> patterns/ukg.json
Footwork  core/50%/snare-family per bar: [(16, 24, 3), (15, 23, 3)]  -> patterns/footwork.json
Stepper   core/50%/snare-family per bar: [(16, 24, 2), (14, 23, 2)]  -> patterns/stepper.json
Electro   core/50%/snare-family per bar: [(16, 21, 0), (15, 24, 2)]  -> patterns/electro.json
Drill     core/50%/snare-family per bar: [(16, 21, 3), (16, 24, 3)]  -> patterns/drill.json
Afro      core/50%/snare-family per bar: [(16, 24, 1), (14, 23, 1)]  -> patterns/afro.json
Trap      core/50%/snare-family per bar: [(15, 23, 2), (15, 24, 2)]  -> patterns/trap.json
```

- [ ] **Step 3: Check one file by eye**

```bash
python3 -c "import json; d=json.load(open('patterns/stepper.json')); print(d['lanes']['kick']['core']); print(d['lanes']['snare']['core'])"
```

Expected:

```
X...X...X...X...|X...X...X...X...
....X.......X...|....X.......X...
```

- [ ] **Step 4: Commit**

```bash
git add tools/seed_patterns.py patterns
git commit -m "feat(cutline): seed 11 factory patterns from the 2.0 style tables"
```

---

### Task 6: Factory pattern lint

**Files:**
- Create: `tests/engine/PatternLintTests.cpp`
- Modify: `tests/CMakeLists.txt` (the `cl-engine-tests` source list and one compile definition)

**Interfaces:**
- Consumes: `parsePattern` (Task 4), `resolveBar`, `ResolvedBar` (Task 2), `patternBars`, `kStepsPerBar`, `kDrumLaneCount`, `Layer` (Task 1), and the files Task 5 generated.
- Produces: a `[patterns]` test that fails if any file in `patterns/` breaks the anchor or a note budget. Plan 2 and the later library spec rely on it to keep new patterns calm.

The test is tagged `[patterns]`, not `[engine]`, so the RealtimeSanitizer suite (which runs `[engine]` only) does not run file I/O.

- [ ] **Step 1: Write the test**

Create `tests/engine/PatternLintTests.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Content lint for every factory pattern in products/cutline/patterns.
#include "cl/groove_json.h"
#include "cl/resolve.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace cl;

namespace {

// Note-ons in a resolved bar: ratchet hits and flams count.
int noteOns(const ResolvedBar& bar, int laneFrom, int laneTo, bool nonCoreOnly, bool fillOnly) {
    int n = 0;
    for (int lane = laneFrom; lane <= laneTo; ++lane) {
        for (const auto& s : bar.lanes[static_cast<std::size_t>(lane)]) {
            if (s.velocity == 0)
                continue;
            if (nonCoreOnly && (s.fromFill || s.layer == Layer::core))
                continue;
            if (fillOnly && !s.fromFill)
                continue;
            n += static_cast<int>(s.ratchet) + (s.flam ? 1 : 0);
        }
    }
    return n;
}

bool isCore(const Step& s) { return s.on() && s.layer == Layer::core; }

} // namespace

TEST_CASE("Every factory pattern keeps its anchor and stays inside the note budgets", "[patterns]") {
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(CL_PATTERNS_DIR)) {
        if (entry.path().extension() != ".json")
            continue;
        ++files;
        const std::string file = entry.path().filename().string();
        INFO(file);

        std::ifstream in(entry.path());
        std::ostringstream text;
        text << in.rdbuf();
        std::string error;
        const auto pattern = parsePattern(text.str(), &error);
        INFO(error);
        REQUIRE(pattern.has_value());

        const int bars = patternBars(*pattern);
        for (int bar = 0; bar < bars; ++bar) {
            INFO("bar " << bar + 1);
            const auto origin = static_cast<std::size_t>(bar * kStepsPerBar);

            // Anchor: kick on beat 1 of every bar, and the same Core snare in every bar.
            CHECK(isCore(pattern->lanes[0].steps[origin]));
            bool snareCore = false;
            for (std::size_t step = 0; step < static_cast<std::size_t>(kStepsPerBar); ++step) {
                const bool first = isCore(pattern->lanes[1].steps[step]);
                CHECK(first == isCore(pattern->lanes[1].steps[origin + step]));
                snareCore = snareCore || first;
            }
            CHECK(snareCore);

            ResolvedBar resolved;
            resolveBar(*pattern, bar, {0.0f, 0.5f, false}, resolved);
            CHECK(noteOns(resolved, 0, kDrumLaneCount - 1, false, false) <= 16);

            resolveBar(*pattern, bar, {0.5f, 0.5f, false}, resolved);
            CHECK(noteOns(resolved, 0, kDrumLaneCount - 1, false, false) <= 24);
            CHECK(noteOns(resolved, 1, 3, true, false) <= 3); // snare, ghost, clap off the anchor
        }

        // Fill: one or two beats, at least one note, at most six per beat.
        CHECK((pattern->fill.beats == 1 || pattern->fill.beats == 2));
        ResolvedBar fill;
        resolveBar(*pattern, bars - 1, {0.5f, 0.5f, true}, fill);
        const int fillNotes = noteOns(fill, 0, kDrumLaneCount - 1, false, true);
        CHECK(fillNotes >= 1);
        CHECK(fillNotes <= 6 * static_cast<int>(pattern->fill.beats));
    }
    CHECK(files == 11);
}
```

In `tests/CMakeLists.txt`, add the file to `cl-engine-tests`, directly after `engine/HitsTests.cpp`:

```cmake
    engine/PatternLintTests.cpp
```

and add this line directly after the `target_link_libraries(cl-engine-tests ...)` line:

```cmake
target_compile_definitions(cl-engine-tests PRIVATE CL_PATTERNS_DIR="${PROJECT_SOURCE_DIR}/patterns")
```

- [ ] **Step 2: Prove the lint can fail**

Break one pattern on purpose. This moves a bar-2 snare hit out of Core:

```bash
python3 -c "
import json
p='patterns/stepper.json'; d=json.load(open(p))
d['lanes']['snare']['core']='....X.......X...|....X...........'
open(p,'w').write(json.dumps(d,indent=2)+'\n')"
cmake --build --preset engine --target cl-engine-tests && ./build/engine/tests/cl-engine-tests "[patterns]"
```

Expected: the run fails, naming `stepper.json` and `bar 2`, on the line `CHECK(first == isCore(pattern->lanes[1].steps[origin + step]))`.

- [ ] **Step 3: Restore the file and verify the lint passes**

```bash
git checkout patterns/stepper.json
./build/engine/tests/cl-engine-tests "[patterns]"
```

Expected: `All tests passed (` followed by an assertion count, `in 1 test case)`.

- [ ] **Step 4: Commit**

```bash
git add tests/engine/PatternLintTests.cpp tests/CMakeLists.txt
git commit -m "test(cutline): lint factory patterns for anchor and note budgets"
```

---

### Task 7: Verify the whole tree

**Files:** none created. This task proves the additive work left Cutline 2.0 untouched.

**Interfaces:**
- Consumes: everything above.
- Produces: a green engine preset, dev preset and RealtimeSanitizer workflow on the branch.

- [ ] **Step 1: Run every engine-preset test**

```bash
cmake --build --preset engine && ctest --preset engine
```

Expected: `100% tests passed`. The count is the 2.0 total plus the 24 new test cases.

- [ ] **Step 2: Build and test the plugin and UI**

```bash
cmake --preset dev -DCPM_SOURCE_CACHE="$HOME/.cache/CPM" && cmake --build --preset dev && ctest --preset dev
```

Expected: `100% tests passed`. Nothing in the plugin or UI changed, so a failure here means an existing file was edited by mistake: check `git diff main --stat` and revert anything outside the File Structure table.

- [ ] **Step 3: Run the RealtimeSanitizer workflow (Linux only)**

```bash
cmake --workflow --preset rtsan
```

Expected on Linux: the workflow finishes with `100% tests passed`. The `rtsan` preset is disabled on macOS (it needs `clang++-22` on Linux), where the command stops with `Cannot use disabled preset`. On a Mac, skip this step: CI runs it on every pull request, and the compile-time `-Werror=function-effects` check already ran in Step 1.

- [ ] **Step 4: Check formatting of the new C++ files**

`clang-format` is not on `PATH` on the development Mac; Xcode's copy is used here and CI runs the pinned one.

```bash
xcrun clang-format --style=file:../../.clang-format --dry-run --Werror \
  core/include/cl/groove.h core/include/cl/groove_json.h core/src/groove_json.cpp core/src/groove_json_detail.h \
  engine/include/cl/resolve.h engine/include/cl/hits.h engine/src/resolve.cpp \
  tests/core/GrooveTests.cpp tests/core/GrooveJsonTests.cpp \
  tests/engine/ResolveTests.cpp tests/engine/HitsTests.cpp tests/engine/PatternLintTests.cpp
```

Expected: no output. If it reports a file, run the same command with `-i` in place of `--dry-run --Werror`, rebuild, re-run Step 1, and commit with `git commit -am "style(cutline): format groove model sources"`.

- [ ] **Step 5: Confirm the change is additive**

Run from the repository root:

```bash
git diff main --stat -- . ':(exclude)products/cutline/docs'
```

Expected: only the files in the File Structure table, plus `core/CMakeLists.txt`, `engine/CMakeLists.txt` and `tests/CMakeLists.txt`. Every other line is a new file.

---

## What Plan 2 does

Plan 2 is written once this plan has landed, against the real code. Its tasks, in order:

1. New parameter contract in `params.h`, and presets and plugin state on `GrooveBank` with `"format": 3`.
2. Engine drum playback from `resolveBar` and `forEachHit`, the four interim pad programs in their own file, and the truth and determinism tests.
3. Slicer compile, clip slice and MIDI export on the new model; delete `styles.cpp`, `tools/seed_patterns.py` and the 2.0 half of `phrase.cpp`.
4. UI port: the grid and ribbon draw `resolveBar` output, layer and fill editing, the new macro row, GRID as the opening view.
5. Plugin glue: telemetry position state, pattern store, factory presets rebuilt from `patterns/`, state versioning.
6. README, release notes and a full verification pass.

## Carried into Plan 2 from the reviews

The whole-branch review approved this plan's work with one fix (the hit-time clamp, done). It left these for Plan 2, which builds the first callers:

- Build each scene's `DensityMap` on the message thread and publish it with the bank. `rankLayer` is an insertion sort and should not run on the audio thread.
- `forEachHit` emits lane by lane, not in time order. Collect into a fixed buffer (at most 640 hits per bar) and sort, or filter by block window.
- MIDI export must handle negative hit times itself (shift the clip or clamp), now that `forEachHit` does not.
- Derive the `fillNow` lookahead from the largest early shift (nudge 0.25 beat, plus lane offset and flam in beats at the current tempo and rate) instead of a fixed half beat.
- Clamp `offsetMs` to -20..+20 on the play path; only the JSON reader clamps it today.
- Pick one rule for steps hidden by a shorter `bars` or `slicerLength`: the writer drops them, memory keeps them.
- JSON strictness: reject an edit whose `step` is out of range instead of clamping it; treat slicer and pattern edits alike; decide whether an edit may switch on a step that has no character.
- Guard `resolveBar` against a NaN `density`.
- Tests to add beside the truth test: `resolveBar` on bar 2 of a multi-bar pattern and with a negative bar, a looped lane crossing a bar line, the full lane-priority order, mute with solo on one lane, mute on fill steps, the ratchet and nudge clamps, the flam velocity floor, long-note lanes, and fill and slicer edits read from JSON.
- Small hygiene: name the long-note lanes (5 and 7) in `hits.h`, include `<cstddef>` where `std::size_t` is used, and comment the Density epsilon in `resolve.cpp`.
- `hits.h` is first compiled under `-Werror=function-effects` when `engine.cpp` includes it. A scratch compile from a nonblocking caller passes on Apple clang 21.

For the library spec: lint anchors through `storedStep()`, turn `files == 11` into a floor, and review by ear the patterns whose Core snare is on beat 2 only (Amen, Drill).

