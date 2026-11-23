// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/overrides.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace cl {

class OverrideStore {
  public:
    explicit OverrideStore(juce::AudioProcessorValueTreeState& state);

    PhraseOverrides get() const;
    void set(const PhraseOverrides& overrides);
    void clear();
    void reloadFromState();

    bool pull(PhraseOverrides& destination) noexcept;

  private:
    void writeToState();

    juce::AudioProcessorValueTreeState& state_;
    mutable juce::SpinLock lock_;
    PhraseOverrides overrides_{};
    std::atomic<bool> changed_{true};
};

} // namespace cl
