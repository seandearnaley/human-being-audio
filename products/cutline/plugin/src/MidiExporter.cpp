// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MidiExporter.h"

#include "cl/smf.h"

#include <hba/host/offline_export.h>

#include <algorithm>

namespace cl {

juce::File MidiExporter::writeTempFile(const Params& params, const LaneCurves& curves, const PatternBank& bank,
                                       const HeldNotes& held, double bpm, int sceneIdx,
                                       const juce::String& presetName) {
    const auto bytes = exportPhraseSmf(params, curves, held, bpm, sceneIdx, bank);
    const auto sceneChar = static_cast<juce::juce_wchar>('A' + std::clamp(sceneIdx, 0, kSceneCount - 1));
    const auto safeName = juce::File::createLegalFileName(presetName.isEmpty() ? juce::String("Cutline") : presetName);
    const auto filename = safeName + " " + juce::String(juce::roundToInt(bpm)) + "bpm Scene-" +
                          juce::String::charToString(sceneChar) + " " +
                          juce::Time::getCurrentTime().formatted("%H%M%S") + ".mid";
    return hba::host::writeTempMidiFile("Cutline", filename, bytes);
}

} // namespace cl
