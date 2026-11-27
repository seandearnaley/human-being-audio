// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dt/engine.h"
#include "dt/ui/model.h"

#include <hba/host/offline_export.h>

namespace dt {

struct ExportRequest {
    Params params = Params::defaults();
    hba::dsp::EnvelopeCurve pitchCurve = hba::dsp::defaultKickPitchCurve();
    hba::dsp::EnvelopeCurve ampCurve = hba::dsp::defaultKickAmpCurve();
    ComposePattern pattern = defaultComposePattern();
    ClickSample click{};
    double sampleRate = 48000.0;
    double bpm = 172.0;
    bool renderPatternLoop = false;
    int slotIndex = 0;
};

class Resampler {
  public:
    Resampler();

    static juce::File directory();

    ui::ExportState state() const noexcept { return worker_.state(); }
    void start(const ExportRequest& request, const juce::String& presetName);
    void invalidate() noexcept { worker_.invalidate(); }
    juce::File file() const { return worker_.file(); }

  private:
    hba::host::OfflineWavWorker<ExportRequest, ui::ExportState> worker_;
};

} // namespace dt
