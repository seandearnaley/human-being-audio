#pragma once

#include "pf/lanes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace pf {

class CurveStore {
public:
    explicit CurveStore(juce::AudioProcessorValueTreeState& state);

    LaneCurves all() const;
    Curve get(int lane) const;
    void set(int lane, const Curve& curve);
    void setAll(const LaneCurves& curves);
    void reloadFromState();
    bool pull(LaneCurves& destination) noexcept;

private:
    void writeToState(int lane);

    juce::AudioProcessorValueTreeState& state_;
    mutable juce::SpinLock lock_;
    LaneCurves curves_ = defaultLaneCurves();
    std::atomic<bool> changed_ { true };
};

}
