// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ClickStore.h"
#include "Parameters.h"
#include "Presets.h"
#include "Resampler.h"
#include "StateStore.h"
#include "StudioHistory.h"
#include "Telemetry.h"
#include "dt/engine.h"

#include <hba/host/sidechain.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace dt {

class PluginProcessor final : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener,
                              private juce::AsyncUpdater {
  public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &auxSidechainExt_; }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.9; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;

    Params currentParams() const noexcept { return values_.snapshot(); }
    double lastBpm() const noexcept { return lastBpm_.load(std::memory_order_relaxed); }

    // Lock-free UI audition request processed on next audio callback.
    void requestAudition(int slotIndex, float velocity) noexcept;

    struct HistorySnapshot {
        std::array<float, kParamCount> params{};
        AudioStateBundle bundle{};
        std::string presetName;

        bool operator==(const HistorySnapshot&) const = default;
    };

    // Declaration order guarantees engine_ outlives clickStore (engine receives callbacks).
    juce::AudioProcessorValueTreeState state;

  private:
    ParameterValues values_;
    Engine engine_;

  public:
    StateStore stateStore;
    ClickStore clickStore;
    Presets presets;
    Resampler resampler;
    TelemetryChannel telemetry;
    hba::studio::History<HistorySnapshot> history;

  private:
    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void prepareEngine();
    Transport readTransport(int numSamples);
    void handleMidi(const juce::MidiMessage& message) noexcept;

    hba::host::AuxSidechainExtension auxSidechainExt_;
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    double standalonePpq_ = 0.0;

    AudioStateBundle audioBundle_{};
    std::array<float, 1024> monoScratchR_{};
    std::array<float, 1024> scScratchL_{};
    std::array<float, 1024> scScratchR_{};

    std::atomic<double> lastBpm_{172.0};
    std::atomic<bool> pendingPrepare_{false};
    std::atomic<int> pendingAuditionSlot_{-2}; // -2 = none, -1 = activeSlot, 0..3 = specific slot
    std::atomic<float> pendingAuditionVel_{0.96f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace dt
