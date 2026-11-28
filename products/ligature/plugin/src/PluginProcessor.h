// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "LinkHub.h"
#include "Parameters.h"
#include "Presets.h"
#include "lg/document.h"
#include "lg/engine.h"
#include "lg/midi_file.h"
#if LG_HAS_STUDIO_VOICES
#include "lg/studio_voices.h"
#endif

#include <hba/host/telemetry_channel.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <bitset>
#include <map>
#include <optional>
#include <vector>

namespace lg {

class PluginProcessor final : public juce::AudioProcessor, private juce::Timer {
  public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
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

    // Message thread.
    Document& document() noexcept { return document_; }
    // After any change to the document: compile and publish to the engine, save into the state, share with followers.
    void songChanged();
    void loadPreset(int index);
    std::string presetName() const;
    void pushCommand(const Command& c);
    EngineTelemetry telemetry() const { return telemetry_.read(); }
    int outputRole() const; // -1 leads
    void setOutputRole(int role);
    bool leaderFound() const;
    std::optional<Chord> playedChord() const; // the chord last held on the MIDI input, if three or more notes
    std::vector<TimedNote> capturedPerformance() const { return capture_; }
    double lastBpm() const noexcept { return lastBpm_.load(std::memory_order_relaxed); }
    float paramValue(ParamId id) const;
    void setParamValue(ParamId id, float value);
    // Message thread, 20 times a second: collects the captured performance and keeps linked instances in step.
    void serviceMessageThread();

    juce::AudioProcessorValueTreeState state;
    Presets presets;

  private:
    struct CaptureEvent {
        double beat = 0.0;
        std::uint8_t status = 0;
        std::uint8_t pitch = 0;
        std::uint8_t velocity = 0;
        bool reset = false; // the transport started: begin a new capture
    };

    void timerCallback() override;
    void publishSong();
    void handleInput(const juce::MidiBuffer& midi, int numSamples, bool perform, bool leading);
    void drainCapture();

    Document document_;
    Engine engine_;
#if LG_HAS_STUDIO_VOICES
    StudioVoices voices_; // installed in the engine; prepared with it
#endif
    ParameterValues values_;
    MidiOutBuffer out_;
    hba::host::TelemetryChannel<EngineTelemetry> telemetry_;

    juce::AbstractFifo commandFifo_{256};
    std::array<Command, 256> commands_{};

    juce::AbstractFifo captureFifo_{8192};
    std::array<CaptureEvent, 8192> captured_{};
    std::vector<TimedNote> capture_;
    std::map<std::pair<int, int>, std::pair<double, int>> captureOpen_; // (channel, pitch) -> (start, velocity)
    bool wasPlaying_ = false;
    double captureBeat_ = 0.0; // the capture clock: host beat at Play, then advanced by every block that plays

    juce::MidiBuffer bypassMidi_;                      // note-offs then the pass-through input, while bypassed
    std::array<std::bitset<128>, 16> passedThrough_{}; // notes whose note-on passed through while bypassed
    std::bitset<128> heldInput_;                       // notes held on the MIDI input (outside the one-key chord range)
    std::atomic<std::uint32_t> heldChord_{0};          // pitch-class mask | lowest note << 16 | count << 24
    int lastScene_ = 0;
    std::uint64_t publishedRevision_ = 0;
    std::uint64_t followerSongGeneration_ = 0;
    std::atomic<double> lastBpm_{122.0};
    double sampleRate_ = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace lg
