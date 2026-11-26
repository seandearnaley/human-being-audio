// Renders the current patch offline at 16x into a float WAV that can be dragged
// into the host. Runs on its own thread; the audio thread is not involved.
#pragma once

#include "tf/render.h"
#include "tf/ui/model.h"

#include <hba/host/offline_export.h>

namespace tf {

class Resampler {
  public:
    Resampler();

    // Ignored while a render is running.
    void start(const RenderRequest& request, const juce::String& presetName);
    void invalidate() noexcept { worker_.invalidate(); }

    ui::ResampleState state() const noexcept { return worker_.state(); }
    // The finished file. Empty unless the state is `ready`.
    juce::File file() const { return worker_.file(); }

    static juce::File directory();

  private:
    hba::host::OfflineWavWorker<RenderRequest, ui::ResampleState> worker_;
};

} // namespace tf
