// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/overrides.h"

#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace cl {

class OverrideStore {
  public:
    explicit OverrideStore(juce::AudioProcessorValueTreeState& state);

    PhraseOverrides get() const;
    void set(const PhraseOverrides& overrides);
    void clear();
    void reloadFromState();

    bool pull(PhraseOverrides& destination) noexcept { return mailbox_.pull(destination); }

  private:
    void writeToState(const PhraseOverrides& overrides);

    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<PhraseOverrides> mailbox_{};
};

} // namespace cl
