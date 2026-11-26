// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "CurveStore.h"
#include "Parameters.h"
#include "Presets.h"
#include "PrintStore.h"
#include "Resampler.h"
#include "StudioHistory.h"
#include "Telemetry.h"
#include "ln/engine.h"

#include <hba/host/midi_expression.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace ln {

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
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;

    struct Performance {
        int note = 29; // F1
        double bpm = 172.0;
    };
    Performance lastPerformance() const noexcept;
    Params currentParams() const noexcept { return values_.snapshot(); }

    void triggerPrint(bool stackGeneration);
    void triggerPrintSync(bool stackGeneration);

    struct HistorySnapshot {
        std::array<float, kParamCount> params{};
        LaneCurves curves = defaultLaneCurves();
        std::string presetName;

        bool operator==(const HistorySnapshot&) const = default;
    };

    // Declaration order guarantees engine_ outlives printStore (worker joins before engine_ destructs).
    juce::AudioProcessorValueTreeState state;

  private:
    ParameterValues values_;
    Engine engine_;

  public:
    CurveStore curves;
    PrintStore printStore;
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

    hba::host::MidiExpressionParser midiParser_{2.0f};
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    double standalonePpq_ = 0.0;

    LaneCurves audioCurves_ = defaultLaneCurves();
    std::vector<float> monoScratchR_;

    std::atomic<int> lastNote_{29};
    std::atomic<double> lastBpm_{172.0};
    std::atomic<bool> pendingPrepare_{false};
    int lastBendChoice_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace ln
