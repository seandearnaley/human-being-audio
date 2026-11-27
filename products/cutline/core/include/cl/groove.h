// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline 3 groove model: literal steps in three layers, one drawn fill per pattern.
// Fixed-size and allocation-free, so it can be copied on the real-time audio thread.
#pragma once

#include "cl/nonblocking.h"
#include "cl/params.h"
#include "cl/pattern.h"

#include <algorithm>
#include <array>
#include <cstddef>
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

} // namespace cl
