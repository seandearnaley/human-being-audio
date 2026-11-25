// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Bouncer.h"
#include "Parameters.h"
#include "Presets.h"
#include "StudioHistory.h"
#include "Telemetry.h"
#include "sb/engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace sb {

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
    double getTailLengthSeconds() const override { return 2.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;

    Params currentParams() const noexcept { return values_.snapshot(); }
    void triggerAudition(Articulation art, float velocity = 0.96f) noexcept;
    void seedPreviewTelemetry();

    struct HistorySnapshot {
        std::array<float, kParamCount> params{};
        std::string presetName;

        bool operator==(const HistorySnapshot&) const = default;
    };

    juce::AudioProcessorValueTreeState state;
    Presets presets;
    Bouncer bouncer;
    TelemetryChannel telemetry;
    hba::studio::History<HistorySnapshot> history;

  private:
    static constexpr std::size_t kAuditionQueueSize = 16;
    static constexpr std::size_t kAuditionQueueMask = kAuditionQueueSize - 1;

    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void prepareEngine();
    void handleMidi(const juce::MidiMessage& message, const Params& params) noexcept;

    ParameterValues values_;
    Engine engine_;
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;

    std::vector<float> monoScratchR_;

    // Lock-free SPSC audition queue from UI thread to audio thread.
    std::array<std::uint32_t, kAuditionQueueSize> audQueue_{};
    std::atomic<std::uint32_t> audWrite_{0};
    std::atomic<std::uint32_t> audRead_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace sb
