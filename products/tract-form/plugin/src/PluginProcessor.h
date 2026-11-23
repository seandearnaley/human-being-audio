#pragma once

#include "Analysis.h"
#include "CurveStore.h"
#include "Parameters.h"
#include "Presets.h"
#include "Resampler.h"
#include "StudioHistory.h"
#include "tf/engine.h"

#include <hba/host/midi_expression.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <string>
#include <vector>

namespace tf {

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
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;

    // What the last block was played with, for Resample.
    struct Performance {
        int note = 29;
        double bpm = 174.0;
    };
    Performance lastPerformance() const noexcept;
    Params currentParams() const noexcept { return values_.snapshot(); }
    void setAudition(bool down) noexcept { auditionWanted_.store(down, std::memory_order_relaxed); }

    struct HistorySnapshot {
        std::array<float, kParamCount> params{};
        LaneCurves curves = defaultLaneCurves();
        std::string presetName;

        bool operator==(const HistorySnapshot&) const = default;
    };

    juce::AudioProcessorValueTreeState state;
    CurveStore curves;
    Presets presets;
    Analysis analysis;
    Resampler resampler;
    hba::studio::History<HistorySnapshot> history;

  private:
    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void prepareEngine();
    Transport readTransport() const;
    void handleMidi(const juce::MidiMessage& message) noexcept;

    ParameterValues values_;
    Engine engine_;
    hba::host::MidiExpressionParser midiParser_{2.0f};
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    std::vector<float> masterTap_, growlTap_;
    LaneCurves audioCurves_ = defaultLaneCurves();

    std::atomic<int> lastNote_ { 29 };
    std::atomic<double> lastBpm_ { 174.0 };
    std::atomic<bool> auditionWanted_{false};
    bool auditionPlaying_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

}
