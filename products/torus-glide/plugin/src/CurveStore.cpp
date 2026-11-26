// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CurveStore.h"

#include <algorithm>

namespace tg {

CurveStore::CurveStore(juce::AudioProcessorValueTreeState& state)
    : store_(state, defaultLaneCurves(), juce::Identifier("TorusGlideCurves"), true) {}

hba::dsp::Curve CurveStore::get(int lane) const { return store_.get(std::clamp(lane, 0, kLaneCount - 1)); }

LaneCurves CurveStore::all() const { return store_.all(); }

void CurveStore::set(int lane, const hba::dsp::Curve& next) { store_.set(std::clamp(lane, 0, kLaneCount - 1), next); }

void CurveStore::setAll(const LaneCurves& next) { store_.setAll(next); }

void CurveStore::reloadFromState() { store_.reloadFromState(); }

} // namespace tg
