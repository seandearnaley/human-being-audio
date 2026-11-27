// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Resampler.h"

#include <hba/host/preset_bank.h>

#include <vector>

namespace dt {

juce::File Resampler::directory() { return hba::host::studioUserDirectory("Datum", "Bounces"); }

Resampler::Resampler()
    : worker_("Datum WAV Bounce", [](const ExportRequest& req) {
          std::vector<float> left, right;
          Engine::renderOffline(req.params, req.pitchCurve, req.ampCurve, req.pattern,
                                req.click.valid() ? &req.click : nullptr, req.sampleRate, req.bpm,
                                req.renderPatternLoop, req.slotIndex, left, right);
          std::vector<float> interleaved(left.size() * 2u);
          for (std::size_t i = 0; i < left.size(); ++i) {
              interleaved[i * 2u] = left[i];
              interleaved[i * 2u + 1u] = right[i];
          }
          return interleaved;
      }) {}

void Resampler::start(const ExportRequest& request, const juce::String& presetName) {
    const auto safe = juce::File::createLegalFileName(presetName.isEmpty() ? juce::String("Datum") : presetName);
    const auto key = juce::String(noteNameForHz(request.params[ParamId::tuneHz])).replace("#", "s");
    const auto tag = request.renderPatternLoop
                         ? (juce::String(juce::roundToInt(request.bpm)) + "bpm_loop")
                         : (juce::String(juce::roundToInt(request.params[ParamId::tuneHz])) + "Hz_" + key);
    const auto name = safe + "_" + tag + "_" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav";
    worker_.start(request, directory().getChildFile(name), juce::roundToInt(request.sampleRate));
}

} // namespace dt
