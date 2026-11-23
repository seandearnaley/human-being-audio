// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "CurveStore.h"
#include "MidiExporter.h"
#include "Parameters.h"
#include "Presets.h"
#include "Telemetry.h"
#include "cl/engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace cl {

class PluginProcessor final : public juce::AudioProcessor {
  public:
    PluginProcessor();
    ~PluginProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return wrapperType == wrapperType_AudioUnit; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState state;
    CurveStore curves;
    Presets presets;
    Telemetry telemetry;
    MidiExporter exporter;

  private:
    Transport readTransport(int numSamples);

    ParameterValues values_;
    Engine engine_;
    LaneCurves audioCurves_ = defaultLaneCurves();
    MidiEventBuffer outEvents_{};
    std::vector<float> monoScratchR_;
    double sampleRate_ = 48000.0;
    double standalonePpq_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace cl
