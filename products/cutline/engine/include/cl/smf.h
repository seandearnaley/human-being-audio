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
