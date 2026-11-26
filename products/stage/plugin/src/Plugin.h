// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Capture.h"
#include "Link.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>

namespace stage {

// Built twice. As Stage Link it is an instrument with an optional stereo sidechain: it makes no
// sound and reports its track's transport, MIDI and eight macros. As Stage Sound it is an effect
// for the main track: it passes the mix through untouched and sends it to the Stage.
class Processor final : public juce::AudioProcessor {
  public:
    Processor();
    ~Processor() override;

    // What this track plays: 0 is the drums, 1 to 4 are layers, 5 is the song's sound (Stage Sound
    // only). Saved with the project.
    static constexpr int soundRole = 5;
    void setRole(int role) {
        role_.store(role);
        link_.setRole(role);
    }
    [[nodiscard]] int role() const { return role_.load(); }
    [[nodiscard]] bool hubConnected() const { return link_.hubConnected(); }
    [[nodiscard]] std::uint64_t dropped() const { return capture_.dropped(); }

    void prepareToPlay(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        link_.setSampleRate(sampleRate);
    }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return JucePlugin_IsSynth; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

  private:
    Capture capture_;
    SampleRing samples_;
    Link link_;
    std::uint64_t framesPushed_{0};
    std::array<juce::AudioParameterFloat*, macroCount> macros_{};
    double sampleRate_{48000.0};
    std::atomic<int> role_{1};
    int claimedLayer_{0};
};

} // namespace stage
