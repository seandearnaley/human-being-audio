#pragma once

#include "Analysis.h"
#include "CurveStore.h"
#include "ImprintStore.h"
#include "Parameters.h"
#include "Presets.h"
#include "pf/engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace pf {

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
    double getTailLengthSeconds() const override { return 0.25; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;

    Params currentParams() const noexcept { return values_.snapshot(); }

    juce::AudioProcessorValueTreeState state;
    CurveStore curves;
    ImprintStore imprint;
    Presets presets;
    Analysis analysis;

private:
    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void prepareEngine();
    Transport readTransport() const;
    void handleMidi(const juce::MidiMessage& message) noexcept;

    ParameterValues values_;
    Engine engine_;
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    std::vector<float> monoScratchR_;
    LaneCurves audioCurves_ = defaultLaneCurves();
    ImprintData audioImprint_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

}
