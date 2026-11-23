#pragma once

#include "pf/cubes.h"

#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <string>

namespace pf {

class ImprintStore {
public:
    explicit ImprintStore(juce::AudioProcessorValueTreeState& state);

    ImprintData get() const;
    std::string label() const;
    void set(const ImprintData& imprint, std::string sourceName);
    void clear();
    void reloadFromState();
    bool pull(ImprintData& destination) noexcept { return mailbox_.pull(destination); }

  private:
    void writeToState(const ImprintData& imprint, const std::string& label);

    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<ImprintData> mailbox_{};
    mutable juce::SpinLock labelLock_;
    std::string label_{"Imprint WAV"};
};

}
