// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Resampler.h"

#include <hba/core/note_name.h>
#include <hba/host/preset_bank.h>

namespace tg {

juce::File Resampler::directory() { return hba::host::studioUserDirectory("Torus Glide", "Resamples"); }

Resampler::Resampler()
    : worker_("Torus Glide resample", [](const RenderRequest& req) { return renderOfflineLoop(req); }) {}

void Resampler::start(const RenderRequest& request, const juce::String& presetName) {
    const auto safe = juce::File::createLegalFileName(presetName.isEmpty() ? juce::String("Torus Glide") : presetName);
    const auto name = safe + " " + juce::String(hba::core::midiNoteName(request.note, true)) + " " +
                      juce::String(juce::roundToInt(request.bpm)) + "bpm " +
                      juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav";
    RenderRequest req = request;
    req.oversampling = 8;
    worker_.start(req, directory().getChildFile(name), juce::roundToInt(req.sampleRate));
}

} // namespace tg
