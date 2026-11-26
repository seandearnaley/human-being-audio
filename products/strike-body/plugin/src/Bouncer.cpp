// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Bouncer.h"

#include <hba/host/preset_bank.h>

namespace sb {

juce::File Bouncer::directory() { return hba::host::studioUserDirectory("Strike Body", "One-Shots"); }

Bouncer::Bouncer() : worker_("Strike Body bounce", [](const OneShotRequest& req) { return renderOneShot(req); }) {}

void Bouncer::start(const OneShotRequest& request, const juce::String& presetName) {
    const std::string stem = keyTaggedStem(presetName.isEmpty() ? std::string("Strike Body") : presetName.toStdString(),
                                           request.params.real(ParamId::bodyHz));
    const auto safe = juce::File::createLegalFileName(juce::String(stem));
    const auto name = safe + "_" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav";
    OneShotRequest req = request;
    req.oversampling = 8;
    req.normalizePeak = true;
    worker_.start(req, directory().getChildFile(name), juce::roundToInt(req.sampleRate));
}

} // namespace sb
