// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "pv/preset.h"

#include <hba/host/curve_store.h>
#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>

namespace pv {

struct AudioExtraState {
    std::uint16_t chordMask = kDefaultChordMask;
    LaneCurves curves = defaultLaneCurves();

    bool operator==(const AudioExtraState&) const = default;
};

class StateStore {
  public:
    explicit StateStore(juce::AudioProcessorValueTreeState& state);

    std::uint16_t chordMask() const;
    void setChordMask(std::uint16_t mask);

    hba::dsp::Curve laneCurve(int lane) const;
    void setLaneCurve(int lane, const hba::dsp::Curve& curve);

    AudioExtraState all() const;
    void setAll(const AudioExtraState& extra);
    void reloadFromState();

    // Audio thread non-blocking pull.
    bool pull(std::uint16_t& chordMaskOut, LaneCurves& curvesOut) noexcept;

  private:
    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<std::uint16_t> chordMailbox_;
    hba::host::CurveStore<kLaneCount> curveStore_;
};

} // namespace pv
