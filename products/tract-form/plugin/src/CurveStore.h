// The lanes' drawn curves. They are not host parameters: they live in the
// session state, are edited on the message thread, and reach the audio thread
// through hba::host::StateMailbox.
#pragma once

#include "tf/lanes.h"

#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace tf {

class CurveStore {
public:
    explicit CurveStore(juce::AudioProcessorValueTreeState& state);

    LaneCurves all() const;
    Curve get(int lane) const;
    void set(int lane, const Curve& curve);
    void setAll(const LaneCurves& curves);
    void reloadFromState();

    bool pull(LaneCurves& destination) noexcept { return mailbox_.pull(destination); }

  private:
    void writeToState(int lane, const Curve& curve);

    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<LaneCurves> mailbox_{defaultLaneCurves()};
};

}
