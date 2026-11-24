// SPDX-License-Identifier: GPL-3.0-or-later

#include "PluginProcessor.h"

#include "HostSync.h"
#include "PadSurface.h"
#include "PluginEditor.h"

#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

LivecutAudioProcessor::LivecutAudioProcessor(livecut::spectral::TestHooks* hooks)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Livecut", livecut::createParameterLayout()), settingsReader(parameters),
      presets(parameters), spectralTestHooks(hooks) {
    startTimer (100);
}

namespace
{
class AdoptCallbackLock
{
  public:
    explicit AdoptCallbackLock (const juce::CriticalSection &entered) : lock (entered) {}
    ~AdoptCallbackLock ()
    {
        lock.exit ();
    }
    AdoptCallbackLock (const AdoptCallbackLock &) = delete;
    AdoptCallbackLock &operator= (const AdoptCallbackLock &) = delete;

  private:
    const juce::CriticalSection &lock;
};
} // namespace

void LivecutAudioProcessor::enterReplacementLock ()
{
    // Lifecycle only. A retry must cancel the latest published worker, rather
    // than wait behind an offline callback using a newer uncancelled worker.
    for (;;)
    {
        std::shared_ptr<const livecut::spectral::StopHandle> stop;
        {
            const std::lock_guard lock(stopHandleMutex);
            stop = activeStopPublication;
        }
        if (stop)
            stop->requestStop ();
        if (getCallbackLock ().tryEnter ())
            return;
        std::this_thread::yield ();
    }
}

bool LivecutAudioProcessor::enterCurrentOperation (std::uint64_t operation)
{
    while (configurationGeneration.load (std::memory_order_acquire) == operation)
    {
        if (getCallbackLock ().tryEnter ())
            return true;
        std::this_thread::yield ();
    }
    return false;
}

void LivecutAudioProcessor::publishStopHandle ()
{
    const std::lock_guard lock(stopHandleMutex);
    activeStopPublication = stream ? stream->stopPublication : std::shared_ptr<const livecut::spectral::StopHandle>{};
}

LivecutAudioProcessor::~LivecutAudioProcessor ()
{
    stopTimer ();
    std::unique_ptr<livecut::StreamResources> retired;
    enterReplacementLock ();
    {
        const AdoptCallbackLock guard (getCallbackLock ());
        retired = std::move (stream);
        publishStopHandle ();
    }
    retired.reset ();
}

void LivecutAudioProcessor::prepareToPlay (double sampleRate, int)
{
    std::unique_ptr<livecut::StreamResources> retired;
    std::uint64_t operation;
    enterReplacementLock ();
    {
        const AdoptCallbackLock guard (getCallbackLock ());
        suspendProcessing (true);
        operation = ++configurationGeneration;
        observedTransport = AudioObservation::unknown;
        retired = std::move (stream);
        publishStopHandle ();
        processingSampleRate = sampleRate;
        preparationFailed = false;
        publishedModeState = ModeState::unprepared;
    }
    retired.reset (); // no heavy destruction under our callback guard
    try
    {
        const auto settings = settingsReader.read ();
        auto prepared = std::make_unique<livecut::StreamResources> (sampleRate, settings.requestedMode, settings.requestedSpectral, spectralTestHooks);
        livecut::engine::WetHold preparedHold;
        preparedHold.prepare (sampleRate);
        auto preparedCapture = livecut::PhraseCapture::prepareStorage (sampleRate);
        livecut::PhraseCapture::Prepared retiredCapture;
        if (!enterCurrentOperation (operation))
            return;
        {
            const AdoptCallbackLock guard (getCallbackLock ());
            if (configurationGeneration != operation)
                return;
            padGate.prepare (sampleRate);
            classicRelease = false;
            classicEntryGain = 1.0f;
            classicEntryStep = 1.0f / static_cast<float> (std::lround (.005 * sampleRate));
            currentMapping = livecut::engine::mapPad (padGate.activeProgram (), settings.padX, settings.padY, {});
            const auto effective = effectiveSettings (settings);
            renderSettings = effective; // a first context never counts as a surface switch
            preparedHold.request (effective.hold);
            prepared->cutter.setIdle (settings.surface == livecut::Surface::pad);
            padGain = settings.surface == livecut::Surface::pad ? 0.0f : 1.0f;
            std::swap (wetHold, preparedHold);
            retiredCapture = capture.rolling ().installPrepared (std::move (preparedCapture));
            mixer.prepare(sampleRate, effective.wet, effective.outputTrimDb, effective.subAnchorHz, effective.limiter);
            midiPadMask = 0;
            display.midiPadMask.store(0, std::memory_order_relaxed);
            lastWaveformBin = livecut::DisplayState::waveformBins;
            for (auto& bin : display.waveform)
                bin.store(0.0f, std::memory_order_relaxed);
            for (auto& bin : display.sliceMarks)
                bin.store(0.0f, std::memory_order_relaxed);
            spectralMix.reset (sampleRate, 0.002);
            spectralMix.setCurrentAndTargetValue (effective.spectralMix);
            processingRunning = false;
            standalonePpq = 0.0;
            haveDelayedDisplay = false;
            stream = std::move (prepared);
            publishStopHandle ();
            publishSpectralConfiguration ();
            declaredLatency = stream->latency;
            publishTransformDisplay ();
            ++configurationGeneration;
            display.hold = wetHold.state ();
            display.status = livecut::DisplayState::Status::waitingForHost;
        }
    }
    catch (const std::exception &)
    {
        if (!enterCurrentOperation (operation))
            return;
        const AdoptCallbackLock guard (getCallbackLock ());
        if (configurationGeneration != operation)
            return;
        preparationFailed = true;
        declaredLatency = 0;
        capture.rolling ().interrupt ();
        wetHold.interrupt ();
        display.hold = wetHold.state ();
        display.status = livecut::DisplayState::Status::preparationError;
        publishedModeState = ModeState::error;
    }
    reconcileLatency ();
}

void LivecutAudioProcessor::releaseResources ()
{
    std::unique_ptr<livecut::StreamResources> retired;
    enterReplacementLock ();
    {
        const AdoptCallbackLock guard (getCallbackLock ());
        suspendProcessing (true);
        ++configurationGeneration;
        retired = std::move (stream);
        publishStopHandle ();
        capture.rolling ().interrupt ();
        wetHold.interrupt ();
        haveDelayedDisplay = false;
        display.hold = wetHold.state ();
        observedTransport = AudioObservation::unknown;
        preparationFailed = false;
        publishedModeState = ModeState::unprepared;
    }
    retired.reset ();
    // Deactivation discards PCM, retaining the complete declared L0+Ds contract.
    reconcileLatency ();
}

void LivecutAudioProcessor::reconcileLatency ()
{
    // Claim setter ownership under the guard, then call listeners off-guard.
    // Reentrant/concurrent lifecycle calls return pending instead of invoking
    // another setter while the owner is inside a host notification.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        std::uint64_t generation;
        int desired;
        {
            const juce::ScopedTryLock guard (getCallbackLock ());
            if (!guard.isLocked ())
            {
                applyPending = true;
                publishedModeState = ModeState::pendingHostChange;
                return;
            }
            if (latencyUpdateInProgress)
            {
                applyPending = true;
                publishedModeState = ModeState::pendingHostChange;
                return;
            }
            latencyUpdateInProgress = true;
            generation = configurationGeneration;
            desired = declaredLatency;
        }
        if (getLatencySamples () != desired)
            setLatencySamples (desired);
        {
            const juce::ScopedTryLock guard (getCallbackLock ());
            if (!guard.isLocked ())
            {
                // This setter has returned and remains the sole owner until
                // this release. Leave reconciliation for a later safe attempt.
                applyPending = true;
                publishedModeState = ModeState::pendingHostChange;
                latencyUpdateInProgress.store (false, std::memory_order_release);
                return;
            }
            latencyUpdateInProgress = false;
            if (generation != configurationGeneration)
                continue;
            if (getLatencySamples () != desired)
                continue;
            publishedLatency.store (desired, std::memory_order_release);
            publishedGeneration = configurationGeneration;
            if (stream != nullptr)
            {
                publishedMode.store (stream->mode, std::memory_order_release);
                publishedSpectral.store (stream->spectralEnabled, std::memory_order_release);
                publishedModeState = ModeState::active;
                const auto requested = settingsReader.read ();
                applyPending = requested.requestedMode != stream->mode || requested.requestedSpectral != stream->spectralEnabled;
            }
            else
            {
                publishedModeState = preparationFailed ? ModeState::error : ModeState::unprepared;
                applyPending = false;
            }
            suspendProcessing (false); // null resources deliberately produce gated output
            return;
        }
    }
    applyPending = true;
    publishedModeState = ModeState::pendingHostChange;
}

void LivecutAudioProcessor::applyRequestedMode ()
{
    applyPending = true;
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating ();
    if (messages != nullptr && messages->isThisTheMessageThread ())
        tryApplyMode ();
}

void LivecutAudioProcessor::timerCallback ()
{
    if (applyPending.load (std::memory_order_acquire))
        tryApplyMode ();
}

void LivecutAudioProcessor::tryApplyMode ()
{
    const auto requested = settingsReader.read ();
    double rate = 0.0;
    std::uint64_t generation = 0;
    bool reconcile = false;
    {
        const juce::ScopedTryLock guard (getCallbackLock ());
        if (!guard.isLocked ())
            return;
        if (latencyUpdateInProgress)
            return; // the setter owner must publish or leave reconciliation pending
        // Configuration coherence precedes EVERY mode-choice eligibility return,
        // including null/error resources and unavailable transport evidence.
        reconcile = publishedGeneration != configurationGeneration;
        if (!reconcile)
        {
            if (stream == nullptr)
            {
                publishedModeState = preparationFailed ? ModeState::error : ModeState::unprepared;
                applyPending = false; // a future prepare applies the persisted request
                return;
            }
            if (stream->mode == requested.requestedMode && stream->spectralEnabled == requested.requestedSpectral && !stream->fault &&
                (!stream->spectral || stream->spectral->fault () == livecut::spectral::Fault::none))
            {
                publishedModeState = ModeState::active;
                applyPending = false;
                return;
            }
            const auto observation = observedTransport.load (std::memory_order_acquire);
            if (observation != AudioObservation::stopped)
            {
                publishedModeState = observation == AudioObservation::playing ? ModeState::pendingPlaying : ModeState::pendingObservation;
                return;
            }
            generation = configurationGeneration;
            rate = processingSampleRate;
        }
    }
    if (reconcile)
    {
        reconcileLatency ();
        return;
    }
    std::unique_ptr<livecut::StreamResources> prepared;
    try
    {
        prepared = std::make_unique<livecut::StreamResources> (rate, requested.requestedMode, requested.requestedSpectral, spectralTestHooks);
    }
    catch (const std::exception &)
    {
        const juce::ScopedTryLock guard (getCallbackLock ());
        if (!guard.isLocked ())
            return;
        if (generation != configurationGeneration)
            return; // obsolete work cannot publish over the current generation
        if (requested.requestedMode != settingsReader.read ().requestedMode ||
            requested.requestedSpectral != settingsReader.read ().requestedSpectral)
        {
            applyPending = true;
            publishedModeState = ModeState::pendingHostChange;
            return;
        }
        publishedModeState = ModeState::error;
        applyPending = false;
        return;
    }
    std::unique_ptr<livecut::StreamResources> retired;
    {
        const juce::ScopedTryLock guard (getCallbackLock ());
        if (!guard.isLocked ())
            return;
        if (generation != configurationGeneration)
            return; // the newer lifecycle owns both its state and retry flag
        if (publishedGeneration != configurationGeneration || !juce::exactlyEqual(rate, processingSampleRate) ||
            requested.requestedMode != settingsReader.read().requestedMode ||
            requested.requestedSpectral != settingsReader.read().requestedSpectral) {
            applyPending = true;
            publishedModeState = ModeState::pendingHostChange;
            return; // stale prebuild freed after this guard
        }
        const auto observation = observedTransport.load (std::memory_order_acquire);
        if (observation != AudioObservation::stopped)
        {
            publishedModeState = observation == AudioObservation::playing ? ModeState::pendingPlaying : ModeState::pendingObservation;
            return;
        }
        suspendProcessing (true);
        if (stream->spectral)
            stream->spectral->requestStop ();
        retired = std::move (stream);
        stream = std::move (prepared);
        publishStopHandle ();
        publishSpectralConfiguration ();
        haveDelayedDisplay = false;
        declaredLatency = stream->latency;
        publishTransformDisplay ();
        ++configurationGeneration;
        observedTransport = AudioObservation::unknown;
        capture.rolling ().interrupt ();
        wetHold.interrupt ();
        padGate.reset ();
        classicRelease = false;
        classicEntryGain = 1.0f;
        stream->cutter.setIdle (requested.surface == livecut::Surface::pad);
        padGain = requested.surface == livecut::Surface::pad ? 0.0f : 1.0f;
        const auto effective = effectiveSettings (requested);
        wetHold.request (effective.hold);
        mixer.prepare (rate, effective.wet, effective.outputTrimDb);
        spectralMix.reset (rate, 0.002);
        spectralMix.setCurrentAndTargetValue (effective.spectralMix);
        processingRunning = false;
        display.hold = wetHold.state ();
    }
    retired.reset ();
    reconcileLatency ();
}

void LivecutAudioProcessor::observeTransport ()
{
    const auto *head = getPlayHead ();
    const auto info = head != nullptr ? head->getPosition () : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
    observeTransport (info);
}

void LivecutAudioProcessor::observeTransport (const juce::Optional<juce::AudioPlayHead::PositionInfo> &info)
{
    observedTransport.store (!info                   ? AudioObservation::unknown
                             : info->getIsPlaying () ? AudioObservation::playing
                                                     : AudioObservation::stopped,
                             std::memory_order_release);
}

bool LivecutAudioProcessor::isBusesLayoutSupported (const BusesLayout &layouts) const
{
    // Supports Stereo->Stereo and Mono->Stereo (input duplicated to stereo before panning/cutting).
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() &&
           (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void LivecutAudioProcessor::handleMidiInput(const juce::MidiBuffer& midi) {
    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) {
            const int note = msg.getNoteNumber();
            if (note >= 36 && note <= 43) {
                const int prog = note - 36;
                midiPadMask |= static_cast<std::uint8_t>(1u << static_cast<unsigned>(prog));
                midiActiveProgram = prog;
            }
        } else if (msg.isNoteOff()) {
            const int note = msg.getNoteNumber();
            if (note >= 36 && note <= 43) {
                const int prog = note - 36;
                midiPadMask &= static_cast<std::uint8_t>(~(1u << static_cast<unsigned>(prog)));
                if (midiPadMask != 0 && prog == midiActiveProgram) {
                    for (int p = 7; p >= 0; --p) {
                        if ((midiPadMask & static_cast<std::uint8_t>(1u << static_cast<unsigned>(p))) != 0) {
                            midiActiveProgram = p;
                            break;
                        }
                    }
                }
            }
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            midiPadMask = 0;
        } else if (msg.isController()) {
            const int cc = msg.getControllerNumber();
            const float norm = static_cast<float>(msg.getControllerValue()) / 127.0f;
            if (cc == 1)
                midiXOverride.store(norm, std::memory_order_relaxed);
            else if (cc == 74)
                midiYOverride.store(norm, std::memory_order_relaxed);
        } else if (msg.isChannelPressure()) {
            midiYOverride.store(static_cast<float>(msg.getChannelPressureValue()) / 127.0f, std::memory_order_relaxed);
        } else if (msg.isAftertouch()) {
            midiYOverride.store(static_cast<float>(msg.getAfterTouchValue()) / 127.0f, std::memory_order_relaxed);
        }
    }
    display.midiPadMask.store(midiPadMask, std::memory_order_relaxed);
}

void LivecutAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    handleMidiInput(midi);
    process (buffer, false);
}

void LivecutAudioProcessor::processBlockBypassed (juce::AudioBuffer<float> &buffer, juce::MidiBuffer &)
{
    // Keep Live's original host bypass contract. Studio always advances its
    // physical/analysis/render clocks, selecting unity delayed raw at the end.
    if (isSuspended () || stream == nullptr)
    {
        buffer.clear ();
        return;
    }
    if (stream->mode == livecut::ProcessingMode::studio || stream->spectralEnabled)
        process (buffer, true);
    else if (buffer.getNumSamples () > 0)
        observeTransport ();
}

void LivecutAudioProcessor::failStream ()
{
    stream->fault = true;
    capture.rolling ().interrupt ();
    wetHold.interrupt ();
    display.hold = wetHold.state ();
    if (stream->spectral)
        display.spectralFault = stream->spectral->fault ();
    display.status = livecut::DisplayState::Status::streamError;
}

void LivecutAudioProcessor::process (juce::AudioBuffer<float> &buffer, bool bypass)
{
    juce::ScopedNoDenormals noDenormals;
    if (isSuspended () || stream == nullptr)
    {
        buffer.clear ();
        if (stream == nullptr)
            display.status = livecut::DisplayState::Status::preparationError;
        return;
    }
    auto &resources = *stream;
    const auto frames = buffer.getNumSamples();
    // Mono-to-stereo bus support: mirror mono input onto right output channel before processing.
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() >= 2 && frames > 0)
        buffer.copyFrom(1, 0, buffer, 0, 0, frames);
    if (resources.fault || (resources.spectral && resources.spectral->fault () != livecut::spectral::Fault::none))
    {
        if (frames > 0)
            observeTransport ();
        failStream ();
        // Graceful fallback on Spectral worker deadline: pass dry input cleanly instead of brick-muting.
        if (resources.spectral && resources.spectral->fault() == livecut::spectral::Fault::deadline)
            return;
        buffer.clear ();
        return;
    }
    if (frames == 0)
        return;
    livecut::RenderContext input;
    input.settings = settingsReader.read ();
    // Apply MIDI C1-G1 Pad triggering & CC1/CC74/AT -> X/Y when on the Pad surface.
    if (input.settings.surface == livecut::Surface::pad) {
        if (midiPadMask != 0) {
            input.settings.padProgram = static_cast<livecut::engine::PadProgram>(midiActiveProgram);
            input.settings.padTouch = true;
        }
        const float mx = midiXOverride.load(std::memory_order_relaxed);
        const float my = midiYOverride.load(std::memory_order_relaxed);
        if (mx >= 0.0f)
            input.settings.padX = mx;
        if (my >= 0.0f)
            input.settings.padY = my;
    }
    input.bypass = bypass;
    input.resetHold = resetHoldRequested.exchange (false, std::memory_order_acq_rel);
    const auto *head = getPlayHead ();
    const auto info = head != nullptr ? head->getPosition () : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
    observeTransport (info);
    const auto sync = info ? livecut::hostSync (*info) : std::nullopt;
    if (sync)
    {
        input.running = true;
        input.timing = sync->timing;
        input.position = sync->position;
        input.ppq = *info->getPpqPosition ();
    } else if (wrapperType == wrapperType_Standalone && processingSampleRate > 0.0) {
        constexpr double standaloneBpm = 168.0;
        constexpr double standaloneBeatsPerBar = 4.0;
        const auto bar = static_cast<std::int64_t>(std::floor(standalonePpq / standaloneBeatsPerBar));
        input.running = true;
        input.timing = {standaloneBpm, standaloneBeatsPerBar};
        input.position = {bar, standalonePpq - static_cast<double>(bar) * standaloneBeatsPerBar};
        input.ppq = standalonePpq;
        standalonePpq += (standaloneBpm / 60.0 / processingSampleRate) * static_cast<double>(frames);
    }
    if (!resources.appendContext (input, frames, transportEpochCounter))
    {
        failStream ();
        buffer.clear ();
        return;
    }
    struct SpectralCallback
    {
        livecut::spectral::Stage *stage;
        SpectralCallback (livecut::spectral::Stage *s, bool offline) : stage (s)
        {
            if (stage)
                stage->beginCallback (offline ? livecut::spectral::Execution::offline : livecut::spectral::Execution::realtime);
        }
        ~SpectralCallback ()
        {
            if (stage)
                stage->endCallback ();
        }
    } callback (resources.spectral.get (), isNonRealtime ()); // snapshot explicit host mode once
    bool produced = false;
    for (int i = 0; i < frames; ++i)
    {
        auto &left = buffer.getWritePointer (0)[i];
        auto &right = buffer.getWritePointer (1)[i];
        const auto physical = resources.inputFrame;
        if (!resources.cutter.ingestInputFrame (physical, {left, right}) || !resources.detector.process (physical, left, right))
        {
            failStream ();
            buffer.clear (i, frames - i);
            break;
        }
        ++resources.inputFrame;
        const auto rendered = resources.timeline.renderFrame (physical);
        if (!rendered)
        {
            left = right = 0.0f;
            continue;
        }
        if (*rendered != resources.nextRender)
        {
            failStream ();
            buffer.clear (i, frames - i);
            break;
        }
        livecut::spectral::Packet packet;
        packet.sourceFrame = *rendered;
        const juce::ScopedValueSetter<livecut::spectral::Packet *> collecting (collectingPacket, resources.spectral ? &packet : nullptr);
        const juce::ScopedValueSetter<bool> beforePhase (collectingAfter, false);
        const auto firstContextSample = !resources.rendering;
        if (firstContextSample)
        {
            resources.rendering = resources.timeline.lookup (*rendered);
            if (!resources.rendering || resources.rendering->offset != 0)
            {
                failStream ();
                buffer.clear (i, frames - i);
                break;
            }
            const auto &segment = resources.rendering->segment;
            const auto &context = segment.context;
            const auto newEpoch = resources.renderEpoch != segment.transportEpoch;
            resources.renderEpoch = segment.transportEpoch;
            if (newEpoch)
                resources.cutter.resetRendering ();
            if (context.settings.surface == livecut::Surface::pad)
            {
                // renderSettings still holds the surface the last context rendered with.
                const auto fromClassic = renderSettings.surface == livecut::Surface::classic;
                if (newEpoch || !context.running || (fromClassic && !resources.cutter.isCutting ()))
                {
                    // Closed and idle; a latched Hold reopens the gate on the first sample.
                    padGate.resync ();
                    padGain = 0.0f;
                    classicRelease = false;
                    resources.cutter.setIdle (true);
                }
                else if (fromClassic)
                {
                    // Classic was mid-cut: let go like a lifted finger rather than cut to the input.
                    padGate.arm ();
                    padGain = 1.0f;
                    classicRelease = true;
                }
                const auto mapping = livecut::engine::mapPad (padGate.activeProgram (), context.settings.padX,
                                                             context.settings.padY, currentMapping.zones);
                if (padGate.engaged () && !classicRelease && !livecut::engine::sameDiscreteState (mapping, currentMapping))
                    resources.cutter.restartAtNextUnit ();
                currentMapping = mapping;
            }
            else
            {
                // From the Pad surface, Classic's cuts fade in over the input rather than cut to it.
                if (newEpoch || !context.running)
                    classicEntryGain = 1.0f;
                else if (renderSettings.surface == livecut::Surface::pad)
                    classicEntryGain = resources.cutter.isCutting () ? padGain : 0.0f;
                resources.cutter.setIdle (false);
                padGain = 1.0f;
                classicRelease = false;
            }
            processingBeatsPerSample = context.timing.bpm / 60.0 / processingSampleRate;
            processingBeatsPerBar = context.timing.beatsPerBar;
            display.bpm = context.timing.bpm;
            if (!resources.spectral && context.resetHold)
                wetHold.interrupt ();
            applyRenderSettings (context.settings);
            if (!resources.spectral)
                processingRunning = context.running;
            if (context.running)
                resources.cutter.beginRenderContext (context.timing, context.position, *rendered, i, this);
            else if (newEpoch)
                discontinuity (i);
        }
        const auto raw = resources.cutter.rawInputFrame (*rendered);
        if (!raw)
        {
            failStream ();
            buffer.clear (i, frames - i);
            break;
        }
        const auto &context = resources.rendering->segment.context;
        packet.epoch = resources.rendering->segment.transportEpoch;
        if (resources.spectral)
        {
            packet.raw = {raw->left, raw->right};
            packet.spectral = {renderSettings.spectralFreeze, renderSettings.spectralSmearMs, spectralMix.getNextValue ()};
            packet.output = {processingBeatsPerSample, renderSettings.wet, renderSettings.outputTrimDb, context.running,
                             context.bypass, renderSettings.hold, firstContextSample && context.resetHold};
        }
        left = raw->left;
        right = raw->right;
        if (context.running)
        {
            if (context.settings.surface == livecut::Surface::pad)
            {
                if (!resources.cutter.isTimingSupported ())
                {
                    padGate.reset ();
                    padGain = 0.0f;
                    classicRelease = false;
                    resources.cutter.setIdle (true);
                }
                else
                {
                    const auto& settings = context.settings;
                    const auto position = resources.cutter.renderPosition ();
                    const auto step = padGate.process ({false, settings.padTouch, settings.padHold,
                                                       settings.padProgram, settings.padQuantize},
                                                      position.beatsIntoBar, processingBeatsPerBar, processingBeatsPerSample);
                    if (classicRelease && (step.idle || padGate.open ()))
                    {
                        // Faded to the input, or touched: from here the pad's program plays.
                        classicRelease = false;
                        applyRenderSettings (settings);
                    }
                    if (step.programChanged)
                    {
                        currentMapping = livecut::engine::mapPad (padGate.activeProgram (), settings.padX, settings.padY, {-1, -1});
                        applyRenderSettings (settings);
                        // Classic's releasing cuts are not the program's to restart.
                        if (!classicRelease)
                            resources.cutter.restartAtNextUnit ();
                    }
                    resources.cutter.setIdle (step.idle);
                    padGain = step.gain;
                }
            }
            resources.cutter.renderOne (*rendered, left, right, i, this);
        }
        else if (!resources.spectral)
            mixer.process (raw->left, raw->right, left, right);
        if (resources.spectral)
            packet.processed = {left, right};
        else
        {
            produced = true;
            if (context.bypass)
            {
                left = raw->left;
                right = raw->right;
            }
            if (context.running)
                capture.rolling ().push (left, right);
        }
        // Original position at this sample's exclusive end. It cannot be read
        // later from a Cutter whose phrase/context may already have retired.
        const auto position =
            ((static_cast<double>(context.position.bar) - static_cast<double>(resources.cutter.phraseStartBar())) +
             (context.position.beatsIntoBar +
              static_cast<double>(resources.rendering->offset + 1) * processingBeatsPerSample) /
                 context.timing.beatsPerBar) *
            resources.cutter.activeUnitsPerBar();
        processingBeat = context.ppq + static_cast<double> (resources.rendering->offset + 1) * processingBeatsPerSample;
        ++resources.nextRender;
        const auto contextEnded = ++resources.rendering->offset == resources.rendering->segment.frameCount;
        if (contextEnded && context.running)
        {
            collectingAfter = true;
            resources.cutter.endRenderContext (i + 1, this);
            collectingAfter = false;
        }
        if (resources.spectral)
        {
            packet.display = sourceDisplay ();
            packet.display.position = position;
        }
        if (contextEnded)
            resources.rendering.reset ();
        if (resources.spectral)
        {
            const auto result = resources.spectral->process (packet); // sealed after endRenderContext
            if (result.recoveryRequired)
            {
                failStream ();
                // Graceful dry fallback on Spectral deadline miss instead of brick-muting remaining samples.
                if (result.fault == livecut::spectral::Fault::deadline) {
                    left = raw->left;
                    right = raw->right;
                } else {
                    buffer.clear(i, frames - i);
                }
                break;
            }
            if (result.valid)
            {
                produced = true;
                renderSpectralOutput (result, left, right);
            }
            else
                left = right = 0.0f;
        }
        if (!resources.timeline.retireBefore (resources.nextRender))
        {
            failStream ();
            buffer.clear (i, frames - i);
            break;
        }
    }
    publishTransformDisplay ();
    // Publish lock-free output peak, limiter GR, and live phrase waveform / slice-onset overlay.
    float peakL = 0.0f;
    float peakR = 0.0f;
    const float* outL = buffer.getReadPointer(0);
    const float* outR = buffer.getReadPointer(1);
    for (int i = 0; i < frames; ++i) {
        peakL = std::max(peakL, std::abs(outL[i]));
        peakR = std::max(peakR, std::abs(outR[i]));
    }
    display.outputPeakL.store(peakL, std::memory_order_relaxed);
    display.outputPeakR.store(peakR, std::memory_order_relaxed);
    display.limiterGrDb.store(mixer.gainReductionDb(), std::memory_order_relaxed);
    const double totalUnits = static_cast<double>(std::max(1, resources.cutter.phraseBarCount()) *
                                                  std::max(1, resources.cutter.activeUnitsPerBar()));
    const double normPos = std::clamp(resources.cutter.positionInPhrase() / totalUnits, 0.0, 0.9999);
    const auto bin = static_cast<std::size_t>(normPos * static_cast<double>(livecut::DisplayState::waveformBins));
    const float env = std::min(1.0f, std::max(peakL, peakR));
    if (bin != lastWaveformBin) {
        display.waveform[bin].store(env, std::memory_order_relaxed);
        const bool cutEdge = resources.cutter.isCutting() && resources.cutter.player().currentCut() != lastCutCountBin;
        display.sliceMarks[bin].store(cutEdge ? 1.0f : 0.0f, std::memory_order_relaxed);
        lastWaveformBin = bin;
        lastCutCountBin = resources.cutter.player().currentCut();
    } else {
        const float prev = display.waveform[bin].load(std::memory_order_relaxed);
        if (env > prev)
            display.waveform[bin].store(env, std::memory_order_relaxed);
    }
    if (resources.fault)
        display.status = livecut::DisplayState::Status::streamError;
    else if (!produced)
        display.status = livecut::DisplayState::Status::filling;
    else
        publishDisplay ();
}

void LivecutAudioProcessor::dispatchEvent (const livecut::spectral::Event &event)
{
    using Event = livecut::spectral::EventKind;
    switch (event.kind)
    {
    case Event::discontinuity:
        capture.rolling ().interrupt ();
        wetHold.interrupt ();
        break;
    case Event::barStarted:
        wetHold.barStarted ();
        break;
    case Event::phraseCompleted:
        capture.rolling ().completePhrase ();
        wetHold.completePhrase ();
        break;
    case Event::phraseStarted:
        capture.rolling ().startPhrase (event.fullStart);
        wetHold.startPhrase (event.fullStart, event.phraseQuarterNotes);
        break;
    }
}
void LivecutAudioProcessor::collectOrDispatch (livecut::spectral::Event event)
{
    if (!collectingPacket)
    {
        dispatchEvent (event);
        return;
    }
    if (collectingAfter)
        collectingPacket->after.append (event);
    else
        collectingPacket->before.append (event);
}
void LivecutAudioProcessor::discontinuity(int) noexcept HBA_NONBLOCKING {
    collectOrDispatch ({livecut::spectral::EventKind::discontinuity});
}
void LivecutAudioProcessor::barStarted(int, std::int64_t bar) noexcept HBA_NONBLOCKING {
    collectOrDispatch ({livecut::spectral::EventKind::barStarted, false, bar});
}
void LivecutAudioProcessor::phraseCompleted(int) noexcept HBA_NONBLOCKING {
    collectOrDispatch ({livecut::spectral::EventKind::phraseCompleted});
}
void LivecutAudioProcessor::phraseStarted(int, bool fullStart) noexcept HBA_NONBLOCKING {
    collectOrDispatch (
        {livecut::spectral::EventKind::phraseStarted, fullStart, 0, stream->cutter.phraseBarCount () * processingBeatsPerBar});
}
void LivecutAudioProcessor::processOutput(int, float dryLeft, float dryRight, float& left,
                                          float& right) noexcept HBA_NONBLOCKING {
    if (renderSettings.surface == livecut::Surface::pad)
    {
        left = dryLeft + padGain * (left - dryLeft);
        right = dryRight + padGain * (right - dryRight);
    }
    else if (classicEntryGain < 1.0f)
    {
        left = dryLeft + classicEntryGain * (left - dryLeft);
        right = dryRight + classicEntryGain * (right - dryRight);
        if (stream->cutter.isCutting ())
            classicEntryGain = std::min (1.0f, classicEntryGain + classicEntryStep);
    }
    if (collectingPacket)
        return; // collect inner wet; consumers run at its delayed boundary
    wetHold.process (left, right, processingBeatsPerSample);
    mixer.process (dryLeft, dryRight, left, right);
}
void LivecutAudioProcessor::renderSpectralOutput (const livecut::spectral::Result &result, float &left, float &right)
{
    const auto &packet = result.packet;
    const auto &context = packet.output;
    if (context.resetHold)
        wetHold.interrupt ();
    wetHold.request (context.hold);
    mixer.setTargets(context.globalMix, context.trimDb, renderSettings.subAnchorHz, renderSettings.limiter);
    for (std::size_t i = 0; i < packet.before.count; ++i)
        dispatchEvent (packet.before.items[i]);
    const auto wet = result.localWet ();
    left = wet.left;
    right = wet.right;
    if (context.running)
    {
        if (!result.holdEligible ())
            wetHold.startPhrase (false, 0.0); // candidate only, retain held/latest
        wetHold.process (left, right, context.quarterNotesPerSample);
    }
    mixer.process (packet.raw.left, packet.raw.right, left, right);
    if (context.bypass)
    {
        left = packet.raw.left;
        right = packet.raw.right;
    }
    if (context.running)
        capture.rolling ().push (left, right, result.captureHasWarmup ());
    for (std::size_t i = 0; i < packet.after.count; ++i)
        dispatchEvent (packet.after.items[i]);
    delayedDisplay = packet.display;
    haveDelayedDisplay = true;
    processingRunning = context.running;
    display.spectralAvailability = result.availability;
}

void LivecutAudioProcessor::publishSpectralConfiguration ()
{
    display.spectralConfigured = stream->spectralEnabled;
    display.spectralFault = livecut::spectral::Fault::none;
    display.spectralAvailability =
        stream->spectralEnabled ? livecut::spectral::Availability::waiting : livecut::spectral::Availability::disabled;
    display.spectralFftSize = stream->spectral ? stream->spectral->configuration ().size : 0;
    display.spectralHop = stream->spectral ? stream->spectral->configuration ().hop : 0;
    display.spectralDelay = stream->spectral ? stream->spectral->latency () : 0;
    display.transformEligible = display.attackApplied = display.reverseApplied = display.transformIneligible = 0;
    display.transformReason = livecut::DisplayState::TransformReason::off;
}

livecut::spectral::DisplaySnapshot LivecutAudioProcessor::sourceDisplay () const
{
    const auto &cutter = stream->cutter;
    const auto &transforms = cutter.transformStats ();
    const auto &fracture = cutter.fractureDiagnostics ();
    const auto &resonator = cutter.resonator ().diagnostics ();
    const auto &route = cutter.routeDiagnostics ();
    livecut::spectral::DisplaySnapshot result;
    result.transformEligible = transforms.eligible;
    result.attackApplied = transforms.attackAligned;
    result.reverseApplied = transforms.reversed;
    result.transformIneligible = transforms.ineligible;
    result.transformReason = static_cast<int> (transforms.reason);
    result.phraseBars = cutter.phraseBarCount ();
    result.unitsPerBar = cutter.activeUnitsPerBar ();
    result.blockStart = cutter.blockStartUnits ();
    result.blockEnd = cutter.blockEndUnits ();
    result.position = cutter.positionInPhrase ();
    result.beat = processingBeat;
    result.cutting = cutter.isCutting ();
    result.timingSupported = cutter.isTimingSupported ();
    result.cutterError = static_cast<int> (cutter.error ());
    result.fractureFamily = static_cast<int> (fracture.family);
    result.fractureState = static_cast<int> (fracture.state);
    result.fractureVoices = static_cast<int> (fracture.activeVoices);
    result.fractureTapeSpeed = fracture.tapeSpeed;
    result.resonatorState = static_cast<int> (resonator.state);
    result.resonatorRetuning = resonator.retuning;
    result.resonatorPending = resonator.pending;
    result.effectRoute = static_cast<int> (route.active);
    result.requestedEffectRoute = static_cast<int> (route.requested);
    result.effectRouteTransition = route.transitioning;
    return result;
}

void LivecutAudioProcessor::publishSourceDisplay (const livecut::spectral::DisplaySnapshot &source)
{
    display.transformEligible = source.transformEligible;
    display.attackApplied = source.attackApplied;
    display.reverseApplied = source.reverseApplied;
    display.transformIneligible = source.transformIneligible;
    display.transformReason = static_cast<livecut::DisplayState::TransformReason> (source.transformReason);
    display.fractureFamily = static_cast<livecut::engine::FractureProcedure> (source.fractureFamily);
    display.fractureState = static_cast<livecut::engine::FractureState> (source.fractureState);
    display.fractureVoices = source.fractureVoices;
    display.fractureTapeSpeed = source.fractureTapeSpeed;
    display.resonatorState = static_cast<livecut::engine::Resonator::State> (source.resonatorState);
    display.resonatorRetuning = source.resonatorRetuning;
    display.resonatorPending = source.resonatorPending;
    display.effectRoute = static_cast<livecut::engine::EffectRoute> (source.effectRoute);
    display.requestedEffectRoute = static_cast<livecut::engine::EffectRoute> (source.requestedEffectRoute);
    display.effectRouteTransition = source.effectRouteTransition;
    display.phraseBars = source.phraseBars;
    display.unitsPerBar = source.unitsPerBar;
    display.blockStart = source.blockStart;
    display.blockEnd = source.blockEnd;
    display.position = source.position;
    display.beat = source.beat;
}

void LivecutAudioProcessor::publishTransformDisplay ()
{
    if (stream->spectral)
    {
        if (haveDelayedDisplay)
            publishSourceDisplay (delayedDisplay);
    }
    else
        publishSourceDisplay (sourceDisplay ());
}

livecut::Settings LivecutAudioProcessor::effectiveSettings (const livecut::Settings& user)
{
    if (user.surface == livecut::Surface::classic)
        return user;
    if (classicRelease)
    {
        auto settings = user;
        settings.hold = false; // the Pad surface never holds
        return settings;
    }
    return livecut::padSettings (user, currentMapping);
}

void LivecutAudioProcessor::applyRenderSettings (const livecut::Settings& user)
{
    renderSettings = effectiveSettings (user);
    livecut::applySettings (stream->cutter, renderSettings);
    if (stream->spectral)
        spectralMix.setTargetValue (renderSettings.spectralMix);
    else
    {
        mixer.setTargets(renderSettings.wet, renderSettings.outputTrimDb, renderSettings.subAnchorHz,
                         renderSettings.limiter);
        wetHold.request (renderSettings.hold);
    }
}

void LivecutAudioProcessor::publishDisplay ()
{
    using Status = livecut::DisplayState::Status;
    display.padActiveProgram = static_cast<int> (padGate.activeProgram ());
    const auto pending = padGate.pendingProgram ();
    display.padPendingProgram = pending ? static_cast<int> (*pending) : -1;
    display.padEngaged = padGate.engaged ();
    display.hold = wetHold.state ();
    const auto source = stream->spectral ? delayedDisplay : sourceDisplay ();
    publishSourceDisplay (source);
    if (!processingRunning)
    {
        display.status = Status::waitingForHost;
        return;
    }
    const auto error = static_cast<livecut::engine::Cutter::Error> (source.cutterError);
    if (error != livecut::engine::Cutter::Error::none)
    {
        display.status = error == livecut::engine::Cutter::Error::planOverflow ? Status::planOverflow : Status::sourceUnavailable;
        return;
    }
    if (!source.timingSupported)
    {
        display.status = Status::barTooLong;
        return;
    }
    if (!source.cutting)
    {
        display.status = Status::startingSoon;
        return;
    }
    display.status = Status::cutting;
}

juce::AudioProcessorEditor *LivecutAudioProcessor::createEditor ()
{
    return new LivecutAudioProcessorEditor (*this);
}

bool LivecutAudioProcessor::hasEditor () const
{
    return true;
}

const juce::String LivecutAudioProcessor::getName () const
{
    return JucePlugin_Name;
}
bool LivecutAudioProcessor::acceptsMidi() const { return true; }
bool LivecutAudioProcessor::producesMidi () const
{
    return false;
}
// A requested Hold can play indefinitely; JUCE maps infinity to VST3 kInfiniteTail.
double LivecutAudioProcessor::getTailLengthSeconds () const
{
    return std::numeric_limits<double>::infinity ();
}

int LivecutAudioProcessor::getNumPrograms() { return std::max(1, static_cast<int>(presets.all().size())); }
int LivecutAudioProcessor::getCurrentProgram ()
{
    const int idx = presets.currentIndex();
    return idx >= 0 ? idx : 0;
}
void LivecutAudioProcessor::setCurrentProgram(int index) {
    if (index >= 0 && index < static_cast<int>(presets.all().size()))
        presets.load(index);
}
const juce::String LivecutAudioProcessor::getProgramName(int index) {
    if (index >= 0 && index < static_cast<int>(presets.all().size()))
        return juce::String::fromUTF8(presets.all()[static_cast<std::size_t>(index)].name.c_str());
    return "Default";
}
void LivecutAudioProcessor::changeProgramName (int, const juce::String &) {}

LivecutSnapshot LivecutAudioProcessor::captureSnapshot() const {
    LivecutSnapshot snap;
    const auto& params = getParameters();
    snap.values.reserve(static_cast<std::size_t>(params.size()));
    for (auto* p : params)
        snap.values.push_back(p->getValue());
    return snap;
}

void LivecutAudioProcessor::restoreSnapshot(const LivecutSnapshot& snap) {
    const auto& params = getParameters();
    const auto count = std::min(static_cast<std::size_t>(params.size()), snap.values.size());
    for (std::size_t i = 0; i < count; ++i)
        params[static_cast<int>(i)]->setValueNotifyingHost(snap.values[i]);
}

void LivecutAudioProcessor::pushUndoSnapshot() { history.push(captureSnapshot()); }

void LivecutAudioProcessor::pushCoalescedUndo(std::uint64_t tag) {
    history.pushCoalesced(captureSnapshot(), tag, static_cast<std::int64_t>(juce::Time::getMillisecondCounter()));
}

void LivecutAudioProcessor::undo() {
    if (const auto prev = history.undo(captureSnapshot()))
        restoreSnapshot(*prev);
}

void LivecutAudioProcessor::redo() {
    if (const auto next = history.redo(captureSnapshot()))
        restoreSnapshot(*next);
}

void LivecutAudioProcessor::toggleAB() { restoreSnapshot(history.toggleAB(captureSnapshot())); }

void LivecutAudioProcessor::copyAB() { history.copyActiveToOther(captureSnapshot()); }

void LivecutAudioProcessor::rollSeed() {
    pushUndoSnapshot();
    if (auto* p = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(livecut::ids::seed))) {
        const int cur = p->get();
        int next = 1 + static_cast<int>(hba::dsp::laneHash(static_cast<std::uint32_t>(cur) * 2654435761U + 17U) % 16U);
        if (next == cur)
            next = (cur % 16) + 1;
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(next)));
    }
}

void LivecutAudioProcessor::getStateInformation (juce::MemoryBlock &destData)
{
    auto state = parameters.copyState ();
    state.setProperty ("schemaVersion", 9, nullptr);
    state.setProperty ("algorithmVersion", 2, nullptr);
    if (const auto xml = state.createXml ())
        copyXmlToBinary (*xml, destData);
}

void LivecutAudioProcessor::setStateInformation (const void *data, int sizeInBytes)
{
    // State that isn't ours, or isn't readable, leaves the current settings as they are.
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || !xml->hasTagName (parameters.state.getType ()))
        return;
    auto restored = juce::ValueTree::fromXml (*xml);
    // Sessions saved before the pad surface retain their Classic controls.
    if (!restored.getChildWithProperty ("id", livecut::ids::surface).isValid ())
    {
        juce::ValueTree child ("PARAM");
        child.setProperty ("id", livecut::ids::surface, nullptr);
        child.setProperty ("value", 1, nullptr);
        restored.appendChild (child, nullptr);
    }
    // A missing parameter means the default of that release, even when loading
    // an old set into an instance whose newer controls were already adjusted.
    for (auto *parameter : getParameters ())
    {
        const auto *ranged = dynamic_cast<juce::RangedAudioParameter *> (parameter);
        if (ranged == nullptr || restored.getChildWithProperty ("id", ranged->getParameterID ()).isValid ())
            continue;
        juce::ValueTree child ("PARAM");
        child.setProperty ("id", ranged->getParameterID (), nullptr);
        child.setProperty ("value", ranged->convertFrom0to1 (ranged->getDefaultValue ()), nullptr);
        restored.appendChild (child, nullptr);
    }
    parameters.replaceState (restored);
    // APVTS::replaceState skips setValueNotifyingHost when the snapped denormalised
    // value equals unnormalisedValue, leaving discrete/boolean parameters whose raw
    // normalised value drifted (e.g. 0.35 -> false) out of sync with the host.
    for (auto* parameter : getParameters()) {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            ranged != nullptr && ranged->isDiscrete()) {
            if (const auto* raw = parameters.getRawParameterValue(ranged->getParameterID())) {
                const float expected = ranged->convertTo0to1(raw->load(std::memory_order_relaxed));
                if (!juce::approximatelyEqual(ranged->getValue(), expected))
                    ranged->setValueNotifyingHost(expected);
            }
        }
    }
    resetHoldRequested.store (true, std::memory_order_release);
    applyPending.store (true, std::memory_order_release);
}

juce::String LivecutAudioProcessor::holdStatusText () const
{
    using State = livecut::engine::WetHold::State;
    switch (display.hold.load (std::memory_order_relaxed))
    {
    case State::live:
        return "Live input";
    case State::waiting:
        return "Hold waiting for a fresh complete wet phrase";
    case State::pendingEntry:
        return "Hold enters on the next bar — varispeed";
    case State::held:
        return "Audio held — varispeed follows tempo (pitch changes)";
    case State::pendingRelease:
        return "Hold releases on the next bar";
    case State::oversize:
        return "Hold unavailable: wet phrase exceeds prepared capacity";
    }
    return {};
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter ()
{
    return new LivecutAudioProcessor ();
}

juce::String LivecutAudioProcessor::modeStatusText () const
{
    const auto settings = settingsReader.read ();
    const auto requested = settings.requestedMode;
    const auto name = [] (livecut::ProcessingMode mode) { return mode == livecut::ProcessingMode::live ? "Live" : "Studio"; };
    switch (modeState ())
    {
    case ModeState::unprepared:
        return "Awaiting audio preparation";
    case ModeState::error:
        return "Cannot configure audio; check sample rate and available memory, then prepare again";
    case ModeState::pendingObservation:
        return "Apply pending: waiting for explicit stopped transport or audio preparation";
    case ModeState::pendingPlaying:
        return "Apply pending: stop host playback first";
    case ModeState::pendingHostChange:
        return "Apply pending: host audio configuration is changing";
    case ModeState::active:
        break;
    }
    const auto active = activeMode ();
    if (requested != active || settings.requestedSpectral != activeSpectral ())
        return juce::String ("Pending ") + name (requested) + (settings.requestedSpectral ? " + Spectral" : "") + "; active " +
               name (active) + (activeSpectral () ? " + Spectral" : "") + ". Apply while stopped.";
    return juce::String (name (active)) + " active; " + juce::String (publishedLatency.load ()) +
           " samples latency. Host compensation is host-controlled.";
}

juce::String LivecutAudioProcessor::transformStatusText () const
{
    using Reason = livecut::DisplayState::TransformReason;
    juce::String reason;
    switch (display.transformReason.load (std::memory_order_relaxed))
    {
    case Reason::off:
        reason = "Attack Align and Reverse are off";
        break;
    case Reason::awaitingCut:
        reason = "Waiting for the next cut";
        break;
    case Reason::searchPending:
        reason = "Attack search is still analysing; this cut keeps its planned source";
        break;
    case Reason::noAttack:
        reason = "No qualifying attack; this cut keeps its planned source";
        break;
    case Reason::sourceIncomplete:
        reason = "Complete source audio has not arrived for this cut";
        break;
    case Reason::tooLong:
        reason = "This cut needs more source audio than the available budget";
        break;
    case Reason::sourceLost:
        reason = "Selected source was lost; wet audio is gated";
        break;
    case Reason::applied:
        reason = "Selected complete source is fixed for this cut";
        break;
    case Reason::eligibleForward:
        reason = "Source eligible; reverse draw chose forward playback";
        break;
    case Reason::silentCut:
        reason = "Silent cut has no source to transform";
        break;
    case Reason::analysisError:
        reason = "Attack/source metadata unavailable for this cut";
        break;
    }
    return reason + "\nEligible " + juce::String (display.transformEligible.load ()) + " | Attack aligned " +
           juce::String (display.attackApplied.load ()) + " | Reversed " + juce::String (display.reverseApplied.load ()) +
           " | Ineligible " + juce::String (display.transformIneligible.load ());
}

juce::String LivecutAudioProcessor::spectralStatusText () const
{
    using Availability = livecut::spectral::Availability;
    if (display.spectralFault.load () != livecut::spectral::Fault::none)
        return "Spectral error: output gated. Stop the host and Apply to replace the prepared stream.";
    const auto settings = settingsReader.read ();
    if (settings.requestedSpectral != activeSpectral ())
        return "Spectral configuration pending. Apply while the host is stopped.";
    if (!activeSpectral ())
        return "Spectral off; no additional spectral delay.";
    juce::String state;
    switch (display.spectralAvailability.load ())
    {
    case Availability::waiting:
        state = "Waiting for fresh analysis; spectral wet is zero";
        break;
    case Availability::priming:
        state = "Priming spectral overlap; wet coverage is partial";
        break;
    case Availability::ready:
        state = "Spectral analysis ready";
        break;
    case Availability::disabled:
        state = "Awaiting prepared spectral output";
        break;
    }
    if (settings.spectralFreeze)
        state += "; Freeze requested at the next eligible analysis";
    return state + "\nFFT " + juce::String (display.spectralFftSize.load ()) + " / hop " + juce::String (display.spectralHop.load ()) +
           "; additional delay " + juce::String (display.spectralDelay.load ()) + " samples. Short loops may remain Waiting.";
}
