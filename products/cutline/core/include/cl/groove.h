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
