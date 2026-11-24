// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MidiExporter.h"

#include "tg/export.h"

namespace tg {

juce::File MidiExporter::writeTempFile(const Params& params, const LaneCurves& curves, const SceneMatrix& matrix,
                                       int rootNote, double bpm, const juce::String& presetName) {
    const auto bytes = exportMpeSmf(params, curves, matrix, rootNote, bpm);

    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("HumanBeingAudio")
                         .getChildFile("TorusGlide");
    dir.createDirectory();

    const auto safe = juce::File::createLegalFileName(presetName.isEmpty() ? juce::String("Torus Glide") : presetName);
    const auto filename = safe + " " + juce::String(juce::roundToInt(bpm)) + "bpm " +
                          juce::Time::getCurrentTime().formatted("%H%M%S") + ".mid";
    const auto file = dir.getChildFile(filename);
    file.replaceWithData(bytes.data(), bytes.size());
    return file;
}

} // namespace tg
