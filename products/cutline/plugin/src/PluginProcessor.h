// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "CurveStore.h"
#include "MidiExporter.h"
#include "Parameters.h"
#include "PatternStore.h"
#include "Presets.h"
#include "StudioHistory.h"
#include "Telemetry.h"
#include "cl/engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <string>
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
    // Sends a note-off for every note Cutline has sounding, then passes the incoming MIDI through.
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlockBypassed;

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
    void loadDefaultState();

    struct HistorySnapshot {
        std::array<float, kParamCount> params{};
        LaneCurves curves = defaultLaneCurves();
        GrooveBank bank{};
        std::string presetName;

        bool operator==(const HistorySnapshot&) const = default;
    };

    juce::AudioProcessorValueTreeState state;
    CurveStore curves;
    PatternStore patterns;
    Presets presets;
    Telemetry telemetry;
    MidiExporter exporter;
    hba::studio::History<HistorySnapshot> history;

  private:
    Transport readTransport(int numSamples);
    void ingestMidi(const juce::MidiBuffer& midi, const Transport& transport);

    ParameterValues values_;
    Engine engine_;
    LaneCurves audioCurves_ = defaultLaneCurves();
    GrooveState audioGroove_{};
    MidiEventBuffer outEvents_{};
    juce::MidiBuffer bypassMidi_; // note-offs then the pass-through input, while bypassed
    std::vector<float> monoScratchR_;
    double sampleRate_ = 48000.0;
    double standalonePpq_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace cl
