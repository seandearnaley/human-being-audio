#pragma once

#include "pf/cubes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
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
    bool pull(ImprintData& destination) noexcept;

private:
  void writeToState(const ImprintData& imprint, const std::string& label);

  juce::AudioProcessorValueTreeState& state_;
  mutable juce::SpinLock lock_;
  ImprintData imprint_{};
  std::string label_{"Imprint WAV"};
  std::atomic<bool> changed_{true};
};

}
