#include "Resampler.h"

#include <hba/core/note_name.h>
#include <hba/host/preset_bank.h>

namespace tf {

namespace {
constexpr int kOversampling = 16;
constexpr double kBars = 4.0;
} // namespace

juce::File Resampler::directory() { return hba::host::studioUserDirectory("Tract Form", "Resamples", false); }

Resampler::Resampler() : worker_("Tract Form resample", [](const RenderRequest& req) { return renderOffline(req); }) {}

void Resampler::start(const RenderRequest& request, const juce::String& presetName) {
    const auto name = juce::File::createLegalFileName(presetName.isEmpty() ? juce::String("Tract Form") : presetName) +
                      " " + juce::String(hba::core::midiNoteName(request.note, false)) + " " +
                      juce::String(juce::roundToInt(request.bpm)) + "bpm " +
                      juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav";
    RenderRequest req = request;
    req.oversampling = kOversampling;
    req.bars = kBars;
    req.tailSeconds = 0.0; // exactly four bars, so the file loops at the project tempo
    worker_.start(req, directory().getChildFile(name), juce::roundToInt(req.sampleRate));
}

} // namespace tf
