// SPDX-License-Identifier: AGPL-3.0-or-later
// Vary: a deterministic variation of a pattern, written into the grid as an edit the user can see and undo.
#pragma once

#include "cl/groove.h"

namespace cl {

// Writes a variation of `in` to `out`: three moves (shift, add, remove or ornament a Groove or Detail hit) in the
// target bar, which is the last bar of a pattern with two or more bars and the only bar otherwise. Core steps, lane
// settings and the fill never change, and a move that would push a bar past the Density 50% note budgets is skipped.
// The moves are chosen by a hash of the pattern's content, so the same pattern always gives the same variation.
// Pure: no allocation, no state, no <random>.
// Returns false (and leaves `out` equal to `in`) when the pattern offers nothing to vary.
bool vary(const Pattern& in, Pattern& out) noexcept;

// The bar vary() edits.
constexpr int varyTargetBar(const Pattern& p) noexcept { return patternBars(p) - 1; }

// Density 50% budgets for one bar (spec 1 lint): note-ons, and non-Core snare, ghost or clap note-ons.
inline constexpr int kVaryMaxBarNotes = 24;
inline constexpr int kVaryMaxBarBackbeatExtras = 3;

} // namespace cl
