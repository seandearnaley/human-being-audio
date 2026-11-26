// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/pattern.h"

#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace cl {

class PatternStore {
  public:
    explicit PatternStore(juce::AudioProcessorValueTreeState& state);

    PatternBank get() const;
    void set(const PatternBank& bank);
    void clear();
    void reloadFromState();

    bool pull(PatternBank& destination) noexcept { return mailbox_.pull(destination); }

  private:
    void writeToState(const PatternBank& bank);

    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<PatternBank> mailbox_{};
};

} // namespace cl
