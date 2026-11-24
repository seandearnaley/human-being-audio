// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CurveStore.h"

#include <hba/core/preset_json.h>

#include <algorithm>

namespace tg {

namespace {
const juce::Identifier kCurvesNode{"TorusGlideCurves"};
}

CurveStore::CurveStore(juce::AudioProcessorValueTreeState& state) : state_(state) { writeToTree(defaultLaneCurves()); }

hba::dsp::Curve CurveStore::get(int lane) const {
    const auto allCurves = mailbox_.get();
    return allCurves[static_cast<std::size_t>(std::clamp(lane, 0, kLaneCount - 1))];
}

LaneCurves CurveStore::all() const { return mailbox_.get(); }

void CurveStore::set(int lane, const hba::dsp::Curve& next) {
    const int idx = std::clamp(lane, 0, kLaneCount - 1);
    LaneCurves updated = mailbox_.get();
    updated[static_cast<std::size_t>(idx)] = next;
    setAll(updated);
}

void CurveStore::setAll(const LaneCurves& next) {
    mailbox_.set(next);
    writeToTree(next);
}

void CurveStore::writeToTree(const LaneCurves& curves) {
    auto node = state_.state.getOrCreateChildWithName(kCurvesNode, nullptr);
    for (int i = 0; i < kLaneCount; ++i) {
        const juce::Identifier key{hba::core::curveKey(i)};
        node.setProperty(key, juce::String(hba::core::serialiseCurveJson(curves[static_cast<std::size_t>(i)])),
                         nullptr);
    }
}

void CurveStore::reloadFromState() {
    LaneCurves loaded = defaultLaneCurves();
    const auto node = state_.state.getChildWithName(kCurvesNode);
    if (node.isValid()) {
        for (int i = 0; i < kLaneCount; ++i) {
            const juce::Identifier key{hba::core::curveKey(i)};
            if (node.hasProperty(key)) {
                const auto text = node.getProperty(key).toString().toStdString();
                if (const auto parsed = hba::core::parseCurveJson(text))
                    loaded[static_cast<std::size_t>(i)] = *parsed;
            }
        }
    }
    mailbox_.set(loaded);
}

} // namespace tg
