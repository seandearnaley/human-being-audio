// SPDX-License-Identifier: AGPL-3.0-or-later
// 960-PPQ Standard MIDI File binary encoder for Drag-to-DAW and headless CLI export.
#pragma once

#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/phrase.h"

#include <cstdint>
#include <vector>

namespace cl {

inline constexpr int kSmfPpq = 960;

// Renders one complete phrase (notes, microtiming, pitch bends, expression CCs,
// and MPE MCM RPN when in MPE mode) as a standards-compliant SMF Type-0 binary.
std::vector<std::uint8_t> exportPhraseSmf(const Params& params, const LaneCurves& curves, const HeldNotes& held,
                                          double bpm, std::int64_t phraseIndex = 0);

} // namespace cl
