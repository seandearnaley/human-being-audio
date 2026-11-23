// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CurveStore.h"

#include "Parameters.h"
#include "cl/preset.h"

namespace cl {

namespace {
juce::Identifier propertyFor(int lane) { return juce::Identifier("lane" + juce::String(lane + 1) + "Curve"); }
} // namespace

CurveStore::CurveStore(juce::AudioProcessorValueTreeState& state) : state_(state) {}

LaneCurves CurveStore::all() const { return mailbox_.get(); }

Curve CurveStore::get(int lane) const {
    if (lane < 0 || lane >= kLaneCount)
        return {};
    return mailbox_.get()[static_cast<std::size_t>(lane)];
}

void CurveStore::writeToState(int lane, const Curve& curve) {
    state_.state.setProperty(propertyFor(lane), toJuce(serialiseCurve(curve)), nullptr);
}

void CurveStore::set(int lane, const Curve& curve) {
    if (lane < 0 || lane >= kLaneCount)
        return;
    mailbox_.modify([lane, &curve](LaneCurves& curves) { curves[static_cast<std::size_t>(lane)] = curve; });
    writeToState(lane, curve);
}

void CurveStore::setAll(const LaneCurves& curves) {
    mailbox_.set(curves);
    for (int lane = 0; lane < kLaneCount; ++lane)
        writeToState(lane, curves[static_cast<std::size_t>(lane)]);
}

void CurveStore::reloadFromState() {
    LaneCurves loaded = defaultLaneCurves();
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const auto text = state_.state.getProperty(propertyFor(lane)).toString();
        if (text.isEmpty())
            continue;
        if (const auto curve = parseCurve(text.toStdString()))
            loaded[static_cast<std::size_t>(lane)] = *curve;
    }
    mailbox_.set(loaded);
}

} // namespace cl
