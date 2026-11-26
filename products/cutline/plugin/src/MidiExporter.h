// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/lanes.h"
#include "cl/params.h"
#include "cl/pattern.h"
#include "cl/phrase.h"

#include <juce_core/juce_core.h>

namespace cl {

class MidiExporter {
  public:
    juce::File writeTempFile(const Params& params, const LaneCurves& curves, const PatternBank& bank,
                             const HeldNotes& held, double bpm, int sceneIdx, const juce::String& presetName);
};

} // namespace cl
