// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/chord_matrix.h"
#include "tg/params.h"

#include <juce_core/juce_core.h>

namespace tg {

class MidiExporter {
  public:
    static juce::File writeTempFile(const Params& params, const LaneCurves& curves, const SceneMatrix& matrix,
                                    int rootNote, double bpm, const juce::String& presetName);
};

} // namespace tg
