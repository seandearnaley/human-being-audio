// SPDX-License-Identifier: GPL-3.0-or-later
// Shared APVTS-backed modulation curve store bridged to the audio thread via StateMailbox.
#pragma once

#include <hba/core/preset_json.h>
#include <hba/dsp/curve.h>
#include <hba/host/parameters.h>
#include <hba/host/state_mailbox.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>

namespace hba::host {

template <int LaneCount> class CurveStore {
  public:
    using LaneCurves = std::array<hba::dsp::Curve, static_cast<std::size_t>(LaneCount)>;

    // When childNodeName is empty, curve properties live directly on APVTS root (`lane1Curve`..).
    // When non-empty (e.g. "TorusGlideCurves"), they live under that named child ValueTree node.
    CurveStore(juce::AudioProcessorValueTreeState& state, LaneCurves defaults, juce::Identifier childNodeName = {},
               bool writeDefaultsOnInit = false)
        : state_(state), defaults_(defaults), childNodeName_(std::move(childNodeName)), mailbox_(defaults) {
        if (writeDefaultsOnInit)
            writeAllToTree(defaults_);
    }

    LaneCurves all() const { return mailbox_.get(); }

    hba::dsp::Curve get(int lane) const {
        if (lane < 0 || lane >= LaneCount)
            return defaults_[0];
        return mailbox_.get()[static_cast<std::size_t>(lane)];
    }

    void set(int lane, const hba::dsp::Curve& curve) {
        if (lane < 0 || lane >= LaneCount)
            return;
        mailbox_.modify([lane, &curve](LaneCurves& curves) { curves[static_cast<std::size_t>(lane)] = curve; });
        writeOneToTree(lane, curve);
    }

    void setAll(const LaneCurves& curves) {
        mailbox_.set(curves);
        writeAllToTree(curves);
    }

    void reloadFromState() {
        LaneCurves loaded = defaults_;
        const juce::ValueTree node =
            childNodeName_.isValid() ? state_.state.getChildWithName(childNodeName_) : state_.state;
        if (node.isValid()) {
            for (int lane = 0; lane < LaneCount; ++lane) {
                const juce::Identifier key(hba::core::curveKey(lane));
                const auto text = node.getProperty(key).toString();
                if (text.isEmpty())
                    continue;
                if (const auto parsed = hba::core::parseCurveJson(text.toStdString()))
                    loaded[static_cast<std::size_t>(lane)] = *parsed;
            }
        }
        mailbox_.set(loaded);
    }

    // Audio thread non-blocking pull.
    bool pull(LaneCurves& destination) noexcept { return mailbox_.pull(destination); }

  private:
    juce::ValueTree targetTree() {
        return childNodeName_.isValid() ? state_.state.getOrCreateChildWithName(childNodeName_, nullptr) : state_.state;
    }

    void writeOneToTree(int lane, const hba::dsp::Curve& curve) {
        auto tree = targetTree();
        const juce::Identifier key(hba::core::curveKey(lane));
        tree.setProperty(key, toJuce(hba::core::serialiseCurveJson(curve)), nullptr);
    }

    void writeAllToTree(const LaneCurves& curves) {
        auto tree = targetTree();
        for (int lane = 0; lane < LaneCount; ++lane) {
            const juce::Identifier key(hba::core::curveKey(lane));
            tree.setProperty(key, toJuce(hba::core::serialiseCurveJson(curves[static_cast<std::size_t>(lane)])),
                             nullptr);
        }
    }

    juce::AudioProcessorValueTreeState& state_;
    LaneCurves defaults_;
    juce::Identifier childNodeName_;
    StateMailbox<LaneCurves> mailbox_;
};

} // namespace hba::host
