// SPDX-License-Identifier: AGPL-3.0-or-later
// Engine::Impl, shared by engine.cpp (transport, scheduling, emitting) and pad_programs.cpp (the pads).
#pragma once

#include "cl/engine.h"
#include "cl/hits.h"
#include "cl/midi_event.h"
#include "cl/phrase.h"

#include "audition.h"

#include <array>
#include <cstdint>
#include <limits>

namespace cl {

inline constexpr int kMaxActiveNotes = 32;
inline constexpr int kMaxCapturedEvents = 128;
// A block cannot emit more note-ons than the MIDI buffer holds anyway, so this is the real cap.
inline constexpr int kMaxBlockHits = kMaxMidiEventsPerBlock;
// Bars one block can reach into (the bars resolved per block, and the fillNow latches kept).
inline constexpr int kMaxBarsPerBlock = 4;
inline constexpr double kBeatEps = 1.0e-9;

struct Engine::Impl {
    struct ActiveNote {
        bool active = false;
        std::uint8_t channel = 1;
        std::int8_t note = 0;
        double absEndBeat = 0.0;
    };

    struct CapturedEvent {
        double barBeat = 0.0;
        double durBeats = 0.25;
        std::int8_t note = 60;
        std::uint8_t vel = 100;
        std::int64_t barIndex = -1;
        bool active = false;
    };

    // A note this block will start, in host beats.
    struct BlockHit {
        double beat = 0.0;
        double durBeats = 0.1;
        std::int8_t note = 0;
        std::int8_t lane = 0; // drum lane, or the chord voice in MIDI Slicer mode
        std::uint8_t velocity = 0;
        bool drum = true;
        bool padOn = false; // played while the pad is engaged (its Y can bias velocity)
    };

    // The fill decision as latched for one bar.
    struct FillLatch {
        std::int64_t bar = std::numeric_limits<std::int64_t>::min();
        bool on = false;
    };

    // One block's transport and controls, shared by playback and the pad programs.
    struct Block {
        const Params* params = nullptr;
        double rawStart = 0.0;    // host beats at sample 0 of the block
        double windowStart = 0.0; // host beats: notes starting in [windowStart, windowEnd) belong to this block
        double windowEnd = 0.0;
        double beatsPerSample = 0.0;
        int numSamples = 0;
        double rate = 1.0;       // pattern beats per host beat: Rate, times the Rate pad while it is engaged
        double beatsPerMs = 0.0; // pattern beats per millisecond
        NoteMap noteMap = NoteMap::liveRack;
        PadProgram program = PadProgram::repeat;
        bool padOn = false;   // the pad plays this part of the block
        bool fillNow = false; // fillNow, or Drop's right half
        bool isDrum = true;
        float padX = 0.5f;
        float padY = 0.5f;
    };

    double sampleRate = 48000.0;
    LaneCurves curves = defaultLaneCurves();
    GrooveState groove{};
    std::uint32_t grooveRevision = 0;

    // Live MIDI input
    HeldNotes physicalHeld{};
    HeldNotes held{};
    HeldNotes latched{};
    HeldNotes lastEffectiveHeld{};
    bool sustainDown = false;
    bool wasSlicerLatch = false;
    std::uint8_t midiPadHeld = 0;
    int lastMidiPadProg = -1;

    // Rolling one-bar clip capture for SlicerStyle::clipSlice
    std::array<CapturedEvent, kMaxCapturedEvents> captured{};
    int capturedCount = 0;
    int capturedWrite = 0;
    std::int64_t lastCaptureBar = -1;

    std::array<ActiveNote, kMaxActiveNotes> active{};
    std::array<BlockHit, kMaxBlockHits> blockHits{};
    int blockHitCount = 0;

    // Quantized scene and pad state. Switch beats are in window time (the transport minus Shift), on the grid of the
    // pattern as it sounds. A step plays from the scene that owns its grid time: the pending scene from
    // sceneSwitchBeat on, the previous scene before sceneFromBeat, otherwise the current scene.
    int currentScene = 0;
    int pendingSceneIdx = -1;
    double sceneSwitchBeat = 0.0;
    int prevScene = 0;
    double sceneFromBeat = std::numeric_limits<double>::lowest();
    int activeProg = 0;
    int pendingProg = -1;
    bool engaged = false;
    bool pendingEngage = false;
    double padSwitchBeat = 0.0;

    // Repeat: the window that is looping, in pattern beats
    double repeatAnchor = 0.0;
    std::int64_t repeatBar = 0;
    bool repeatFill = false;

    std::array<FillLatch, kMaxBarsPerBlock> fillLatch{};

    // MIDI Slicer: the compiled phrase, and what it was last compiled from
    SlicerPlan slicerPlanData{};
    SlicerInputs slicerKey{};

    // Transport tracking
    bool wasPlaying = false;
    double expectedNextPpq = 0.0;
    double lastTransportPpq = 0.0;
    EngineMode lastEngineMode = EngineMode::drum;
    SlicerStyle lastSlicerStyle = SlicerStyle::stabChop;

    // CC deduplication
    int lastCc74 = -1;
    int lastCc1 = -1;
    int lastCc11 = -1;
    int lastCc71 = -1;
    int lastPressure = -1;
    int lastBend = 8192;

    // Telemetry
    PlayPosition pos{};
    double lastLanePpq = 0.0;
    std::array<float, kLaneCount> phases{};
    std::array<float, kLaneCount> values{};
    std::array<float, kDrumLaneCount> laneFlashes{};
    int lastEventCount = 0;
    std::uint8_t activityMask = 0;
    bool playingState = false;

    AuditionSynth audition{};

    // engine.cpp
    void prepare(double sr) noexcept CL_NONBLOCKING;
    void clearDedupe() noexcept CL_NONBLOCKING;
    void reset() noexcept CL_NONBLOCKING;
    const HeldNotes& effectiveHeld(const Params& params) const noexcept CL_NONBLOCKING;
    const Pattern& pattern() const noexcept CL_NONBLOCKING;
    const DensityMap& densityMap() const noexcept CL_NONBLOCKING;
    void flushAllActive(MidiEventBuffer& outMidi, int sampleOffset) noexcept CL_NONBLOCKING;
    void triggerNote(std::int8_t noteNum, int velocity, int laneHint, double absEndBeat, int offset,
                     MidiEventBuffer& outMidi, bool isDrum) noexcept CL_NONBLOCKING;
    void recordClipNoteOn(int midiNote, int vel, double ppq) noexcept CL_NONBLOCKING;
    void recordClipNoteOff(int midiNote, double ppq) noexcept CL_NONBLOCKING;
    void emitContinuousAt(const Params& params, double tickBeat, int offset, MidiEventBuffer& outMidi, bool padOn,
                          float padX, float padY) noexcept CL_NONBLOCKING;
    int evalVelBias(const Params& params, double beat, bool padOn, float padY) const noexcept CL_NONBLOCKING;
    bool fillActiveFor(const Pattern& pat, std::int64_t absBar, bool fillNow, int everyBars, double scaledEnd,
                       double beatsPerMs) noexcept CL_NONBLOCKING;
    int sceneAtGrid(double gridBeat) const noexcept CL_NONBLOCKING;
    void pushDrumHit(const Block& b, const Pattern& pat, int lane, double hostBeat, double hostDur,
                     int velocity) noexcept CL_NONBLOCKING;
    void playPattern(const Block& b) noexcept CL_NONBLOCKING;
    void pushNoteHit(const Block& b, int note, int voice, double hostBeat, double hostDur,
                     int velocity) noexcept CL_NONBLOCKING;
    void ensureSlicerPlan(const Params& params, double bpm) noexcept CL_NONBLOCKING;
    void playSlicer(const Block& b) noexcept CL_NONBLOCKING;
    void playClip(const Block& b) noexcept CL_NONBLOCKING;
    void playBlock(const Block& b) noexcept CL_NONBLOCKING;
    void releaseEndingBy(const Block& b, double beat, bool strict, MidiEventBuffer& outMidi) noexcept CL_NONBLOCKING;
    void emitBlockHits(const Block& b, double silenceAt, MidiEventBuffer& outMidi) noexcept CL_NONBLOCKING;
    void process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left, float* right,
                 int numSamples) noexcept CL_NONBLOCKING;
    void renderAuditionAudio(const Params& params, float* left, float* right, int numSamples) noexcept CL_NONBLOCKING;

    // pad_programs.cpp
    void beginRepeat(double hostBeat, double rate, bool fillNow, int everyBars) noexcept CL_NONBLOCKING;
    void playRepeat(const Block& b) noexcept CL_NONBLOCKING;
    void playRoll(const Block& b) noexcept CL_NONBLOCKING;
};

// Mathematical modulo: the result is in 0..n-1 for negative `a` too.
constexpr int floorMod(std::int64_t a, int n) noexcept CL_NONBLOCKING { return static_cast<int>(((a % n) + n) % n); }

} // namespace cl
