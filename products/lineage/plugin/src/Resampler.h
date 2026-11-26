// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ln/engine.h"
#include "ln/ui/model.h"

#include <hba/host/offline_export.h>

namespace ln {

struct ExportRequest {
    Params params = Params::defaults();
    LaneCurves curves = defaultLaneCurves();
    PrintBuffer printBuf;
    double sampleRate = 48000.0;
    double bpm = 172.0;
    int note = 29;
    int bars = 2;
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

} // namespace ln
