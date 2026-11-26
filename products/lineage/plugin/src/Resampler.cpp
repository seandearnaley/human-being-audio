// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Resampler.h"

#include <hba/core/note_name.h>
#include <hba/host/preset_bank.h>

#include <vector>

namespace ln {

juce::File Resampler::directory() { return hba::host::studioUserDirectory("Lineage", "Resamples"); }

Resampler::Resampler()
    : worker_("Lineage WAV Bounce", [](const ExportRequest& req) {
          std::vector<float> left, right;
          Engine::renderOffline(req.params, req.curves, req.sampleRate, req.bpm, req.note, req.bars,
                                req.printBuf.valid() ? &req.printBuf : nullptr, left, right);
          std::vector<float> interleaved(left.size() * 2u);
          for (std::size_t i = 0; i < left.size(); ++i) {
              interleaved[i * 2u] = left[i];
              interleaved[i * 2u + 1u] = right[i];
          }
          return interleaved;
      }) {}

void Resampler::start(const ExportRequest& request, const juce::String& presetName) {
    const auto safe = juce::File::createLegalFileName(presetName.isEmpty() ? juce::String("Lineage") : presetName);
    const auto name = safe + " " + juce::String(hba::core::midiNoteName(request.note, true)) + " " +
                      juce::String(juce::roundToInt(request.bpm)) + "bpm " +
                      juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav";
    worker_.start(request, directory().getChildFile(name), juce::roundToInt(request.sampleRate));
}

} // namespace ln
