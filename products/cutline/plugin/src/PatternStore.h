// SPDX-License-Identifier: AGPL-3.0-or-later
// The plugin's groove bank. Edits arrive on the message thread; set() sanitises the bank, ranks every
// scene's density map there, and publishes both to the audio thread through a try-lock mailbox.
#pragma once

#include "cl/groove_state.h"

#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>

namespace cl {

// The plugin-state property that holds the bank as JSON. PatternStore writes and reads it; getStateInformation stamps
// it on the saved copy.
extern const juce::Identifier kGrooveBankProp;

class PatternStore {
  public:
    explicit PatternStore(juce::AudioProcessorValueTreeState& state);

    GrooveState get() const;
    GrooveBank bank() const;
    void set(const GrooveBank& bank);
    void clear();

    // Reads the bank from plugin state. Returns false when it is missing or does not parse;
    // the store then holds the default bank.
    bool reloadFromState();

    // Bumps on every set, clear and reload, so the UI knows when to fetch the state again.
    std::uint32_t revision() const noexcept { return revision_.load(std::memory_order_relaxed); }

    bool pull(GrooveState& destination) noexcept { return mailbox_.pull(destination); }

  private:
    void publish(const GrooveState& groove);

    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<GrooveState> mailbox_{};
    std::atomic<std::uint32_t> revision_{0};
};

} // namespace cl
