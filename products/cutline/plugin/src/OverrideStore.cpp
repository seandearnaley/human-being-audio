// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OverrideStore.h"

#include "Parameters.h"
#include "cl/preset.h"

namespace cl {

namespace {
const juce::Identifier kOverridesProp{"phraseOverrides"};
} // namespace

OverrideStore::OverrideStore(juce::AudioProcessorValueTreeState& state) : state_(state) {}

PhraseOverrides OverrideStore::get() const {
    const juce::SpinLock::ScopedLockType scoped(lock_);
    return overrides_;
}

void OverrideStore::writeToState() {
    if (overrides_.hasAny())
        state_.state.setProperty(kOverridesProp, toJuce(serialiseOverrides(overrides_)), nullptr);
    else
        state_.state.removeProperty(kOverridesProp, nullptr);
}

void OverrideStore::set(const PhraseOverrides& overrides) {
    {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        overrides_ = overrides;
    }
    writeToState();
    changed_.store(true, std::memory_order_release);
}

void OverrideStore::clear() { set(PhraseOverrides{}); }

void OverrideStore::reloadFromState() {
    PhraseOverrides loaded{};
    const auto text = state_.state.getProperty(kOverridesProp).toString();
    if (text.isNotEmpty()) {
        if (const auto parsed = parseOverrides(text.toStdString()))
            loaded = *parsed;
    }
    {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        overrides_ = loaded;
    }
    changed_.store(true, std::memory_order_release);
}

bool OverrideStore::pull(PhraseOverrides& destination) noexcept {
    if (!changed_.load(std::memory_order_acquire))
        return false;
    const juce::SpinLock::ScopedTryLockType scoped(lock_);
    if (!scoped.isLocked())
        return false;
    destination = overrides_;
    changed_.store(false, std::memory_order_release);
    return true;
}

} // namespace cl
