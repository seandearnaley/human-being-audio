// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "CaptureService.h"
#include "DisplayState.h"
#include "OutputMixer.h"
#include "Parameters.h"
#include "Presets.h"
#include "StreamResources.h"
#include "engine/Cutter.h"
#include "engine/WetHold.h"

#include <StudioHistory.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <mutex>
#include <vector>

struct LivecutSnapshot {
    std::vector<float> values;
    bool operator==(const LivecutSnapshot&) const = default;
};

class LivecutAudioProcessor final : public juce::AudioProcessor, private livecut::engine::Cutter::Observer, private juce::Timer
{
  public:
    // Optional worker test seam is borrowed until processor destruction; factory passes nullptr.
    explicit LivecutAudioProcessor (livecut::spectral::TestHooks* hooks = nullptr);
    ~LivecutAudioProcessor () override;
    enum class ModeState
    {
        unprepared,
        active,
        pendingObservation,
        pendingPlaying,
        pendingHostChange,
        error
    };
    void applyRequestedMode ();
    livecut::ProcessingMode activeMode () const
    {
        return publishedMode.load (std::memory_order_acquire);
    }
    ModeState modeState () const
    {
        return publishedModeState.load (std::memory_order_acquire);
    }
    int reportedLatencySamples () const
    {
        return publishedLatency.load (std::memory_order_acquire);
    }
    juce::String modeStatusText () const;
    juce::String transformStatusText () const;
    bool activeSpectral () const
    {
        return publishedSpectral.load (std::memory_order_acquire);
    }
    int spectralAddedLatencySamples () const
    {
        return display.spectralDelay.load (std::memory_order_acquire);
    }
    juce::String spectralStatusText () const;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources () override;
    bool isBusesLayoutSupported (const BusesLayout &layouts) const override;

    void processBlock (juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi) override;
    void processBlockBypassed (juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor *createEditor () override;
    bool hasEditor () const override;

    const juce::String getName () const override;
    bool acceptsMidi () const override;
    bool producesMidi () const override;
    double getTailLengthSeconds () const override;

    int getNumPrograms () override;
    int getCurrentProgram () override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String &newName) override;

    void getStateInformation (juce::MemoryBlock &destData) override;
    void setStateInformation (const void *data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState &getParameterState ()
    {
        return parameters;
    }
    const livecut::engine::Cutter &getCutter () const
    {
        jassert (stream != nullptr);
        return stream->cutter;
    }
    const livecut::DisplayState &getDisplayState () const
    {
        return display;
    }
    livecut::CaptureService &getCaptureService ()
    {
        return capture;
    }
    livecut::Presets& getPresets() { return presets; }
    const livecut::Presets& getPresets() const { return presets; }
    LivecutSnapshot captureSnapshot() const;
    void restoreSnapshot(const LivecutSnapshot&);
    void pushUndoSnapshot();
    void pushCoalescedUndo(std::uint64_t tag);
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    void undo();
    void redo();
    bool isSlotB() const { return history.isSlotB(); }
    void toggleAB();
    void copyAB();
    void rollSeed();
    void clearMidiAxisOverride() {
        midiXOverride.store(-1.0f, std::memory_order_relaxed);
        midiYOverride.store(-1.0f, std::memory_order_relaxed);
    }
    juce::String holdStatusText () const;

  private:
    void timerCallback () override;
    void tryApplyMode ();
    void reconcileLatency ();
    void observeTransport ();
    void observeTransport (const juce::Optional<juce::AudioPlayHead::PositionInfo> &);
    void handleMidiInput(const juce::MidiBuffer& midi);
    void process (juce::AudioBuffer<float> &, bool bypass);
    livecut::Settings effectiveSettings (const livecut::Settings& user);
    void applyRenderSettings (const livecut::Settings& user);
    void publishDisplay ();
    void publishTransformDisplay ();
    void discontinuity(int) noexcept HBA_NONBLOCKING override;
    void barStarted(int, std::int64_t) noexcept HBA_NONBLOCKING override;
    void phraseCompleted(int) noexcept HBA_NONBLOCKING override;
    void phraseStarted(int, bool) noexcept HBA_NONBLOCKING override;
    void collectOrDispatch (livecut::spectral::Event);
    void dispatchEvent (const livecut::spectral::Event &);
    void renderSpectralOutput (const livecut::spectral::Result &, float &, float &);
    void publishSpectralConfiguration ();
    livecut::spectral::DisplaySnapshot sourceDisplay () const;
    void publishSourceDisplay (const livecut::spectral::DisplaySnapshot &);
    void failStream ();
    void enterReplacementLock ();
    bool enterCurrentOperation (std::uint64_t);
    void publishStopHandle ();
    void processOutput(int, float dryLeft, float dryRight, float& left, float& right) noexcept HBA_NONBLOCKING override;

    juce::AudioProcessorValueTreeState parameters;
    livecut::SettingsReader settingsReader;
    livecut::Presets presets;
    hba::studio::History<LivecutSnapshot> history;
    livecut::spectral::TestHooks* spectralTestHooks = nullptr;
    std::unique_ptr<livecut::StreamResources> stream;
    livecut::DisplayState display;
    livecut::OutputMixer mixer;
    livecut::CaptureService capture;
    livecut::engine::WetHold wetHold;
    livecut::engine::PadGate padGate;
    livecut::engine::PadMapping currentMapping = livecut::engine::mapPad (padGate.activeProgram (), .5f, .5f, {});
    livecut::Settings renderSettings;
    float padGain = 0.0f;
    // Classic's cuts, releasing on the Pad surface after a switch. They keep
    // Classic's settings until the gate closes or the pad is touched.
    bool classicRelease = false;
    // Classic's first cut after an idle pad fades in over the input: 0 until
    // the cutter starts, then up to 1 over 5 ms. 1 when not entering.
    float classicEntryGain = 1.0f, classicEntryStep = 1.0f;
    double processingSampleRate = 48000.0, processingBeatsPerSample = 0.0, processingBeatsPerBar = 4.0;
    double processingBeat = 0.0; // the host's place, in quarter notes, at the end of the last sample rendered
    double standalonePpq = 0.0;  // internal transport fallback for Standalone builds without a host playhead
    std::atomic<bool> resetHoldRequested{false};
    std::uint64_t transportEpochCounter = 1;
    bool processingRunning = false;
    livecut::spectral::Packet *collectingPacket = nullptr;
    bool collectingAfter = false, haveDelayedDisplay = false;
    livecut::spectral::DisplaySnapshot delayedDisplay;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spectralMix{1.0f};
    // Lifecycle-only mutex-protected stop handle; audio never acquires this lock.
    mutable std::mutex stopHandleMutex;
    std::shared_ptr<const livecut::spectral::StopHandle> activeStopPublication;
    // Real-time MIDI note pad performance state (C1..G1 = notes 36..43, CC1 -> X, CC74/AT -> Y).
    std::uint8_t midiPadMask = 0;
    int midiActiveProgram = 0;
    std::atomic<float> midiXOverride{-1.0f};
    std::atomic<float> midiYOverride{-1.0f};
    std::size_t lastWaveformBin = livecut::DisplayState::waveformBins;
    std::size_t lastCutCountBin = 0;
    std::atomic<bool> publishedSpectral{false};
    std::atomic<livecut::ProcessingMode> publishedMode{livecut::ProcessingMode::live};
    std::atomic<ModeState> publishedModeState{ModeState::unprepared};
    enum class AudioObservation
    {
        unknown,
        stopped,
        playing
    };
    std::atomic<AudioObservation> observedTransport{AudioObservation::unknown};
    std::atomic<bool> applyPending{false};
    std::atomic<int> publishedLatency{0};
    std::atomic<std::uint64_t> configurationGeneration{0}; // mutated under callback guard
    std::uint64_t publishedGeneration = 0;                 // callback-guard protected
    int declaredLatency = 0;                               // callback-guard protected; retained during normal host deactivation
    std::atomic<bool> latencyUpdateInProgress{false};
    bool preparationFailed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivecutAudioProcessor)
};
