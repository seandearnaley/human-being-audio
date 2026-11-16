// The lanes' drawn curves. They are not host parameters: they live in the
// session state, are edited on the message thread, and reach the audio thread
// through a copy it takes only when it can do so without waiting.
#pragma once

#include "tf/lanes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace tf {

class CurveStore {
public:
    explicit CurveStore(juce::AudioProcessorValueTreeState& state);

    // Message thread.
    LaneCurves all() const;
    Curve get(int lane) const;
    void set(int lane, const Curve& curve);
    void setAll(const LaneCurves& curves);
    // Re-reads the curves after the session state has been replaced.
    void reloadFromState();

    // Audio thread. Copies the curves into `destination` when they have changed
    // and returns true; returns false at once if there is nothing new or the
    // message thread is mid-edit (it will be picked up on the next block).
    bool pull(LaneCurves& destination) noexcept;

private:
    void writeToState(int lane);

    juce::AudioProcessorValueTreeState& state_;
    LaneCurves curves_ = defaultLaneCurves();
    mutable juce::SpinLock lock_;
    std::atomic<bool> changed_ { true };
};

}
