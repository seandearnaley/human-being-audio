#pragma once

#include "pf/cubes.h"

#include <hba/host/state_mailbox.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <string>

namespace pf {

class ImprintStore {
public:
    explicit ImprintStore(juce::AudioProcessorValueTreeState& state);

    ImprintData get() const;
    std::string label() const;
    std::string slotLabel(int slot) const;
    std::array<std::string, kNumImprintSlots> slotLabels() const;
    void set(const ImprintData& imprint, std::string sourceName);
    void setAll(const ImprintData& imprint, const std::array<std::string, kNumImprintSlots>& labels);
    void setSlot(int slot, const CornerSpec& corner, std::string sourceName);
    void clearSlot(int slot);
    void clear();
    void reloadFromState();
    bool pull(ImprintData& destination) noexcept { return mailbox_.pull(destination); }

  private:
    void writeToState(const ImprintData& imprint, const std::array<std::string, kNumImprintSlots>& labels);

    juce::AudioProcessorValueTreeState& state_;
    hba::host::StateMailbox<ImprintData> mailbox_{};
    mutable juce::SpinLock labelLock_;
    std::array<std::string, kNumImprintSlots> slotLabels_{};
};

}
