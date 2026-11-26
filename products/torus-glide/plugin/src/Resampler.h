// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/export.h"
#include "tg/ui/model.h"

#include <hba/host/offline_export.h>

namespace tg {

class Resampler {
  public:
    Resampler();

    static juce::File directory();

    ui::ResampleState state() const noexcept { return worker_.state(); }
    void start(const RenderRequest& request, const juce::String& presetName);
    void invalidate() noexcept { worker_.invalidate(); }
    juce::File file() const { return worker_.file(); }

  private:
    hba::host::OfflineWavWorker<RenderRequest, ui::ResampleState> worker_;
};

} // namespace tg
