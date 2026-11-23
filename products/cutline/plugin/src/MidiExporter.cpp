// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MidiExporter.h"

#include "cl/smf.h"

#include <algorithm>

namespace cl {

juce::File MidiExporter::writeTempFile(const Params& params, const LaneCurves& curves, const PhraseOverrides& overrides,
                                       const HeldNotes& held, double bpm, std::int64_t phraseIndex,
                                       const juce::String& presetName) {
    const auto bytes = exportPhraseSmf(params, curves, held, bpm, phraseIndex, overrides);

    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("HumanBeingAudio")
                         .getChildFile("Cutline");
    dir.createDirectory();

    const int seed = std::clamp(params.integer(ParamId::seed), 1, 999);
    const auto safeName = juce::File::createLegalFileName(presetName.isEmpty() ? juce::String("Cutline") : presetName);
    const auto filename = safeName + " " + juce::String(juce::roundToInt(bpm)) + "bpm S" + juce::String(seed) + " " +
                          juce::Time::getCurrentTime().formatted("%H%M%S") + ".mid";
    const auto file = dir.getChildFile(filename);
    file.replaceWithData(bytes.data(), bytes.size());
    return file;
}

} // namespace cl
