// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline 3 real-time MIDI engine. Drum notes come only from resolveBar and forEachHit.
// Pure C++20, no JUCE, [[clang::nonblocking]] verified.
#pragma once

#include "cl/groove_state.h"
#include "cl/held_notes.h"
#include "cl/lanes.h"
#include "cl/midi_event.h"
#include "cl/nonblocking.h"
#include "cl/params.h"
#include "cl/pattern.h"
#include "cl/phrase.h"
#include "cl/sent_notes.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace cl {

struct Transport {
    double bpm = 172.0;
    double ppq = 0.0;
    bool playing = false;
};

enum ActivityBit : std::uint8_t {
    kActNote = 1u << 0u,
    kActCc = 1u << 1u,
    kActPad = 1u << 2u,
    kActSlice = 1u << 3u,
    kActClip = 1u << 4u,
};

// Where the engine is in the active scene's pattern.
struct PlayPosition {
    std::int64_t absBar = 0;  // floor(ppq * rate / 4)
    int barInPattern = 0;     // absBar mod the pattern's bars
    int stepInBar = 0;        // 0..15
    bool fillLive = false;    // this bar plays its fill
    int repeatStartStep = -1; // step in the pattern where the Repeat window starts; -1 when Repeat is not engaged
    double repeatBeats = 0.0; // length of the Repeat window in beats
    // The Density and Dynamics the drums resolve with: the parameters, or the Density pad's X and Y while it is held.
    // The grid and ribbon resolve with these, so they show what plays.
    float density = 0.5f;
    float dynamics = 0.5f;
    float rate = 1.0f;      // pattern beats per host beat: Rate, times the Rate pad while it is engaged
    float beatInBar = 0.0f; // 0..4 on the pattern's clock, at the start of the last block
};

class Engine {
  public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept CL_NONBLOCKING;

    void setLaneCurves(const LaneCurves& curves) noexcept CL_NONBLOCKING;
    void setGroove(const GrooveState& groove) noexcept CL_NONBLOCKING;

    // Live MIDI input: C3-F3 engage the six pad programs when Pad MIDI is on (drum mode); in MIDI Slicer
    // mode the notes are the chord to slice, or the clip to capture.
    void inputNoteOn(int midiNote, int velocity, double ppq = -1.0) noexcept CL_NONBLOCKING;
    void inputNoteOff(int midiNote, double ppq = -1.0) noexcept CL_NONBLOCKING;
    void inputSustain(bool down) noexcept CL_NONBLOCKING;
    void inputAllNotesOff() noexcept CL_NONBLOCKING;

    // Writes sample-accurate MIDI into `outMidi` and, when `left` and `right` are not null, the audition kit.
    void process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left, float* right,
                 int numSamples) noexcept CL_NONBLOCKING;

    // Bypass: writes a note-off for every note Cutline has sounding into `outMidi` (cleared first), at sample 0, and
    // forgets the transport, so the next process() starts afresh instead of catching up on the bypassed time.
    void releaseAll(MidiEventBuffer& outMidi) noexcept CL_NONBLOCKING;

    // Telemetry for the 60 Hz UI.
    // The note-ons the last process() call sent, drum and slicer alike, in time order.
    std::span<const SentNote> sentNotes() const noexcept CL_NONBLOCKING;
    PlayPosition position() const noexcept CL_NONBLOCKING;
    const SlicerPlan& slicerPlan() const noexcept CL_NONBLOCKING;
    HeldNotes activeHeldNotes() const noexcept CL_NONBLOCKING;
    int activeScene() const noexcept CL_NONBLOCKING;
    int pendingScene() const noexcept CL_NONBLOCKING;
    int activePadProgram() const noexcept CL_NONBLOCKING;
    int pendingPadProgram() const noexcept CL_NONBLOCKING;
    bool padEngaged() const noexcept CL_NONBLOCKING;
    std::uint8_t midiPadMask() const noexcept CL_NONBLOCKING;
    int capturedClipCount() const noexcept CL_NONBLOCKING;
    double lanePpq() const noexcept CL_NONBLOCKING;
    std::array<float, kLaneCount> lanePhases() const noexcept CL_NONBLOCKING;
    std::array<float, kLaneCount> laneValues() const noexcept CL_NONBLOCKING;
    std::array<float, kDrumLaneCount> laneHitFlashes() const noexcept CL_NONBLOCKING;
    std::uint64_t activeNotesLowMask() const noexcept CL_NONBLOCKING;
    std::uint64_t activeNotesHighMask() const noexcept CL_NONBLOCKING;
    int lastBlockEventCount() const noexcept CL_NONBLOCKING;
    std::uint8_t lastActivityMask() const noexcept CL_NONBLOCKING;
    bool isPlaying() const noexcept CL_NONBLOCKING;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cl
