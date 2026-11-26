// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/export.h"
#include "sb/ui/model.h"

#include <hba/host/offline_export.h>

namespace sb {

class Bouncer {
  public:
    Bouncer();

    static juce::File directory();

    ui::BounceState state() const noexcept { return worker_.state(); }
    void start(const OneShotRequest& request, const juce::String& presetName);
    void invalidate() noexcept { worker_.invalidate(); }
    juce::File file() const { return worker_.file(); }

  private:
    hba::host::OfflineWavWorker<OneShotRequest, ui::BounceState> worker_;
};

} // namespace sb
