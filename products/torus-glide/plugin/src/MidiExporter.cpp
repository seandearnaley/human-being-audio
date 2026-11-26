// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MidiExporter.h"

#include "tg/export.h"

#include <hba/host/offline_export.h>

namespace tg {

juce::File MidiExporter::writeTempFile(const Params& params, const LaneCurves& curves, const SceneMatrix& matrix,
                                       int rootNote, double bpm, const juce::String& presetName) {
    const auto bytes = exportMpeSmf(params, curves, matrix, rootNote, bpm);
    const auto safe = juce::File::createLegalFileName(presetName.isEmpty() ? juce::String("Torus Glide") : presetName);
    const auto filename = safe + " " + juce::String(juce::roundToInt(bpm)) + "bpm " +
                          juce::Time::getCurrentTime().formatted("%H%M%S") + ".mid";
    return hba::host::writeTempMidiFile("TorusGlide", filename, bytes);
}

} // namespace tg
