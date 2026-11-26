#include "CurveStore.h"

namespace tf {

CurveStore::CurveStore(juce::AudioProcessorValueTreeState& state) : store_(state, defaultLaneCurves()) {}

LaneCurves CurveStore::all() const { return store_.all(); }

Curve CurveStore::get(int lane) const { return store_.get(lane); }

void CurveStore::set(int lane, const Curve& curve) { store_.set(lane, curve); }

void CurveStore::setAll(const LaneCurves& curves) { store_.setAll(curves); }

void CurveStore::reloadFromState() { store_.reloadFromState(); }

} // namespace tf
