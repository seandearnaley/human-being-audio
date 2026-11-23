// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/lanes.h"
#include "cl/overrides.h"
#include "cl/params.h"
#include "cl/phrase.h"

#include <juce_core/juce_core.h>

#include <cstdint>

namespace cl {

class MidiExporter {
  public:
    // Writes the exact currently displayed/playing phrase to a clean temporary .mid file
    // ready for OS drag-to-DAW (including active held/latched notes, overrides, and phrase index).
    juce::File writeTempFile(const Params& params, const LaneCurves& curves, const PhraseOverrides& overrides,
                             const HeldNotes& held, double bpm, std::int64_t phraseIndex,
                             const juce::String& presetName);
};

} // namespace cl
