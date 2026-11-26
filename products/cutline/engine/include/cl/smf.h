// SPDX-License-Identifier: AGPL-3.0-or-later
// Deterministic 960-PPQ Standard MIDI File (SMF Type 0) exporter for Cutline 2.0.
#pragma once

#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/pattern.h"
#include "cl/phrase.h"

#include <cstdint>
#include <vector>

namespace cl {

inline constexpr std::uint16_t kSmfPpq = 960;

// Compiles the active scene's 2/4/8-bar drum or MIDI slicer phrase into an
// exact 960-PPQ SMF Type 0 byte buffer ready for drag-and-drop into Live 12.
std::vector<std::uint8_t> exportPhraseSmf(const Params& params, const LaneCurves& curves, const HeldNotes& held,
                                          double bpm = 172.0, int sceneIdx = 0, const PatternBank& bank = {});

} // namespace cl
