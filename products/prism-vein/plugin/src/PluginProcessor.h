// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Parameters.h"
#include "Presets.h"
#include "StateStore.h"
#include "StudioHistory.h"
#include "Telemetry.h"
#include "pv/engine.h"

#include <hba/host/midi_expression.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace pv {

class PluginProcessor final : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener,
                              private juce::AsyncUpdater {
  public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 1.8; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;

    Params currentParams() const noexcept { return values_.snapshot(); }

    struct HistorySnapshot {
        std::array<float, kParamCount> params{};
        AudioExtraState extra{};
        std::string presetName;

        bool operator==(const HistorySnapshot&) const = default;
    };

    juce::AudioProcessorValueTreeState state;

  private:
    ParameterValues values_;
    Engine engine_;

  public:
    StateStore stateStore;
    Presets presets;
    TelemetryChannel telemetry;
    hba::studio::History<HistorySnapshot> history;

  private:
    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void schedulePrepare();
    void prepareEngine();
    Transport readTransport(int numSamples);
    void handleMidi(const juce::MidiMessage& message) noexcept;

    hba::host::MidiExpressionParser midiParser_{2.0f};
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    double standalonePpq_ = 0.0;

    std::uint16_t audioChordMask_ = kDefaultChordMask;
    LaneCurves audioCurves_ = defaultLaneCurves();
    std::vector<float> monoScratchR_;
    std::vector<float> scScratchL_;
    std::vector<float> scScratchR_;

    std::atomic<bool> pendingPrepare_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace pv
