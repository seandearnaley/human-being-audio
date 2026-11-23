// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/phrase.h"

#include <juce_core/juce_core.h>

namespace cl {

class MidiExporter {
  public:
    // Writes the current phrase to a clean temporary .mid file ready for OS drag-to-DAW.
    juce::File writeTempFile(const Params& params, const LaneCurves& curves, double bpm,
                             const juce::String& presetName);
};

} // namespace cl
