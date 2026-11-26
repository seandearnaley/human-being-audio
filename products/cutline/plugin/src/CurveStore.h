// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/lanes.h"

#include <hba/host/curve_store.h>

namespace cl {

class CurveStore {
  public:
    explicit CurveStore(juce::AudioProcessorValueTreeState& state);

    LaneCurves all() const;
    Curve get(int lane) const;
    void set(int lane, const Curve& curve);
    void setAll(const LaneCurves& curves);
    void reloadFromState();

    bool pull(LaneCurves& destination) noexcept { return store_.pull(destination); }

  private:
    hba::host::CurveStore<kLaneCount> store_;
};

} // namespace cl
