#include "CurveStore.h"

#include "Parameters.h"
#include "pf/preset.h"

namespace pf {

namespace {
juce::Identifier propertyFor(int lane) { return juce::Identifier("lane" + juce::String(lane + 1) + "Curve"); }
}

CurveStore::CurveStore(juce::AudioProcessorValueTreeState& state) : state_(state) {}

LaneCurves CurveStore::all() const {
    const juce::SpinLock::ScopedLockType scoped(lock_);
    return curves_;
}

Curve CurveStore::get(int lane) const {
    const juce::SpinLock::ScopedLockType scoped(lock_);
    return curves_[static_cast<std::size_t>(lane)];
}

void CurveStore::writeToState(int lane) {
    state_.state.setProperty(propertyFor(lane), toJuce(serialiseCurve(curves_[static_cast<std::size_t>(lane)])), nullptr);
}

void CurveStore::set(int lane, const Curve& curve) {
    if (lane < 0 || lane >= kLaneCount) return;
    {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        curves_[static_cast<std::size_t>(lane)] = curve;
    }
    writeToState(lane);
    changed_.store(true);
}

void CurveStore::setAll(const LaneCurves& curves) {
    {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        curves_ = curves;
    }
    for (int lane = 0; lane < kLaneCount; ++lane) writeToState(lane);
    changed_.store(true);
}

void CurveStore::reloadFromState() {
    LaneCurves loaded = defaultLaneCurves();
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const auto text = state_.state.getProperty(propertyFor(lane)).toString();
        if (text.isEmpty()) continue;
        if (const auto curve = parseCurve(text.toStdString())) loaded[static_cast<std::size_t>(lane)] = *curve;
    }
    {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        curves_ = loaded;
    }
    changed_.store(true);
}

bool CurveStore::pull(LaneCurves& destination) noexcept {
    if (!changed_.load(std::memory_order_acquire)) return false;
    const juce::SpinLock::ScopedTryLockType scoped(lock_);
    if (!scoped.isLocked()) return false;
    destination = curves_;
    changed_.store(false, std::memory_order_release);
    return true;
}

}
