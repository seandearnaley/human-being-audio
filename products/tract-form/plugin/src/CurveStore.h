// The lanes' drawn curves. They are not host parameters: they live in the
// session state, are edited on the message thread, and reach the audio thread
// through hba::host::CurveStore.
#pragma once

#include "tf/lanes.h"

#include <hba/host/curve_store.h>

namespace tf {

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

} // namespace tf
