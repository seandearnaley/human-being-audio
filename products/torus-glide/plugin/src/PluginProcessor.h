// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "CurveStore.h"
#include "MatrixStore.h"
#include "Parameters.h"
#include "Presets.h"
#include "Resampler.h"
#include "StudioHistory.h"
#include "Telemetry.h"
#include "tg/engine.h"

#include <hba/host/midi_expression.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace tg {

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
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;

    struct Performance {
        int note = 41;
        double bpm = 172.0;
    };
    Performance lastPerformance() const noexcept;
    Params currentParams() const noexcept { return values_.snapshot(); }

    void setAudition(bool active) noexcept { auditionWanted_.store(active, std::memory_order_relaxed); }
    bool isAuditioning() const noexcept { return auditionWanted_.load(std::memory_order_relaxed); }
    void clearKeySwitch() noexcept { clearKeySwitchRequested_.store(true, std::memory_order_release); }

    struct HistorySnapshot {
        std::array<float, kParamCount> params{};
        LaneCurves curves = defaultLaneCurves();
        SceneMatrix matrix = matrixForPack(0);
        std::string presetName;

        bool operator==(const HistorySnapshot&) const = default;
    };

    juce::AudioProcessorValueTreeState state;
    CurveStore curves;
    MatrixStore matrix;
    Presets presets;
    Resampler resampler;
    TelemetryChannel telemetry;
    hba::studio::History<HistorySnapshot> history;

  private:
    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void prepareEngine();
    Transport readTransport(int numSamples);
    void handleMidi(const juce::MidiMessage& message, bool allowKeySwitch) noexcept;

    ParameterValues values_;
    std::atomic<double> pipelineTailSeconds_{0.0};
    Engine engine_;
    hba::host::MidiExpressionParser midiParser_{2.0f};
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    double standalonePpq_ = 0.0;

    LaneCurves audioCurves_ = defaultLaneCurves();
    SceneMatrix audioMatrix_ = matrixForPack(0);
    OutMidiBuffer outMidi_{};
    juce::MidiBuffer generatedMidi_{};
    std::array<float, 1024> monoScratchR_{};

    std::atomic<int> lastNote_{41};
    std::atomic<double> lastBpm_{172.0};
    std::atomic<bool> auditionWanted_{false};
    std::atomic<bool> clearKeySwitchRequested_{false};
    std::atomic<bool> pendingPrepare_{false};
    std::atomic<int> pendingPack_{-1};
    bool auditionPlaying_ = false;
    int auditionNote_ = 41;
    std::atomic<bool> restoringState_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace tg
