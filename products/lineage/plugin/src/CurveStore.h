// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ln/lanes.h"

#include <hba/host/curve_store.h>

namespace ln {

class CurveStore {
  public:
    explicit CurveStore(juce::AudioProcessorValueTreeState& state);

    Curve get(int lane) const;
    LaneCurves all() const;
    void set(int lane, const Curve& next);
    void setAll(const LaneCurves& next);
    void reloadFromState();

    // Audio thread lock-free pull
    bool pull(LaneCurves& destination) noexcept { return store_.pull(destination); }

  private:
    hba::host::CurveStore<kLaneCount> store_;
};

} // namespace ln
