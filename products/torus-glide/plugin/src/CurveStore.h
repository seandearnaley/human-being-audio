// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/params.h"

#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace tg {

class CurveStore {
  public:
    explicit CurveStore(juce::AudioProcessorValueTreeState& state);

    hba::dsp::Curve get(int lane) const;
    LaneCurves all() const;
    void set(int lane, const hba::dsp::Curve& next);
    void setAll(const LaneCurves& next);
    void reloadFromState();

    // Audio thread non-blocking pull
    bool pull(LaneCurves& destination) noexcept { return mailbox_.pull(destination); }

  private:
    void writeToTree(const LaneCurves& curves);

    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<LaneCurves> mailbox_{defaultLaneCurves()};
};

} // namespace tg
