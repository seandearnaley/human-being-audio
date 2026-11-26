// SPDX-License-Identifier: AGPL-3.0-or-later
// Curated 2-bar (32-step) breakbeat, club, and polyrhythmic style library,
// Bjorklund Euclidean mask generator, and 6 drum fill archetypes.
#pragma once

#include "cl/nonblocking.h"
#include "cl/params.h"
#include "cl/pattern.h"

#include <array>
#include <cstdint>

namespace cl::styles {

struct StyleKit {
    // 32-step (2-bar) bitmasks per lane: bit 0 = Bar 1 step 0 .. bit 31 = Bar 2 step 15.
    std::array<std::uint32_t, kDrumLaneCount> coreMask{};   // Backbone (active even at low Complex)
    std::array<std::uint32_t, kDrumLaneCount> grooveMask{}; // Full studio groove (Complex >= 0.30)
    std::array<std::uint32_t, kDrumLaneCount> spiceMask{};  // Extra syncopation (Complex >= 0.60)
    std::array<std::uint32_t, kDrumLaneCount> accentMask{}; // Velocity punch accents
    std::array<std::uint8_t, kDrumLaneCount> defaultLen{};  // Polymetric step length per lane (1..32)
    std::array<double, kDrumLaneCount> pocketMs{};          // Authentic per-lane microtiming feel (+ms = laid back)
    std::uint32_t slicerStabMask = 0;                       // 32-step rhythmic chop pattern for MIDI Slicer
    std::uint32_t slicerAccentMask = 0;                     // Accent steps for MIDI Slicer
    FillType defaultFill = FillType::snareRoll;
};

// Pure zero-allocation Euclidean pulse generator (Bjorklund / Bresenham distribution).
std::uint32_t euclidMask(int hits, int steps, int rotation = 0) noexcept CL_NONBLOCKING;

// Access one of the 12 built-in multi-bar styles.
const StyleKit& styleKit(DrumStyle style) noexcept CL_NONBLOCKING;

// Resolve effective default Euclidean hit count for a lane in Euclidean Lab mode.
int euclideanDefaultHits(int lane, float complexity) noexcept CL_NONBLOCKING;
int euclideanDefaultRot(int lane, int variation) noexcept CL_NONBLOCKING;

} // namespace cl::styles
