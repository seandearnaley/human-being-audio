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
