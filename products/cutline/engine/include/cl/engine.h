// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline 2.0 real-time MIDI breakbeat, polymeter, playable X/Y performance & live MIDI slicer engine.
// Pure C++20, zero JUCE dependencies, [[clang::nonblocking]] verified.
#pragma once

#include "cl/lanes.h"
#include "cl/midi_event.h"
#include "cl/nonblocking.h"
#include "cl/params.h"
#include "cl/pattern.h"
#include "cl/phrase.h"

#include <array>
#include <cstdint>
#include <memory>

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

class Engine {
  public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept CL_NONBLOCKING;

    void setLaneCurves(const LaneCurves& curves) noexcept CL_NONBLOCKING;
    void setPatternBank(const PatternBank& bank) noexcept CL_NONBLOCKING;

    // Live MIDI input (triggers X/Y Pad Programs C3-G3 in Drum Mode, or feeds held notes/clips in MIDI Slicer Mode).
    void inputNoteOn(int midiNote, int velocity, double ppq = -1.0) noexcept CL_NONBLOCKING;
    void inputNoteOff(int midiNote, double ppq = -1.0) noexcept CL_NONBLOCKING;
    void inputSustain(bool down) noexcept CL_NONBLOCKING;
    void inputAllNotesOff() noexcept CL_NONBLOCKING;

    // Generates sample-accurate MIDI into `outMidi` and (optionally) renders the
    // 8-voice audition drum kit / clean stab synth into `left`/`right` when non-null and enabled.
    void process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left, float* right,
                 int numSamples) noexcept CL_NONBLOCKING;

    // Lock-free telemetry snapshots for the 60 Hz Visage UI.
    const PhrasePlan& currentPlan() const noexcept CL_NONBLOCKING;
    HeldNotes activeHeldNotes() const noexcept CL_NONBLOCKING;
    std::int64_t currentPhraseIndex() const noexcept CL_NONBLOCKING;
    int activeScene() const noexcept CL_NONBLOCKING;
    int pendingScene() const noexcept CL_NONBLOCKING;
    int activePadProgram() const noexcept CL_NONBLOCKING;
    int pendingPadProgram() const noexcept CL_NONBLOCKING;
    bool padEngaged() const noexcept CL_NONBLOCKING;
    std::uint8_t midiPadMask() const noexcept CL_NONBLOCKING;
    int capturedClipCount() const noexcept CL_NONBLOCKING;
    double playheadPhraseBeats() const noexcept CL_NONBLOCKING;
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
