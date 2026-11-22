// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline real-time MIDI phrase-grammar scheduler & lightweight audition synth.
// Pure C++20, zero JUCE dependencies, [[clang::nonblocking]] verified.
#pragma once

#include "cl/lanes.h"
#include "cl/midi_event.h"
#include "cl/nonblocking.h"
#include "cl/params.h"
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

class Engine {
  public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept CL_NONBLOCKING;

    void setLaneCurves(const LaneCurves& curves) noexcept CL_NONBLOCKING;

    // Live MIDI input (used when InputMode::liveMidi is active).
    void inputNoteOn(int midiNote, int velocity) noexcept CL_NONBLOCKING;
    void inputNoteOff(int midiNote) noexcept CL_NONBLOCKING;
    void inputAllNotesOff() noexcept CL_NONBLOCKING;

    // Generates sample-accurate MIDI into `outMidi` and (optionally) renders the
    // lightweight audition voice into `left`/`right` when non-null and enabled.
    void process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left, float* right,
                 int numSamples) noexcept CL_NONBLOCKING;

    // Lock-free telemetry snapshots for the Visage UI.
    const PhrasePlan& currentPlan() const noexcept CL_NONBLOCKING;
    double playheadPhraseBeats() const noexcept CL_NONBLOCKING;
    double lanePpq() const noexcept CL_NONBLOCKING;
    std::array<float, kLaneCount> lanePhases() const noexcept CL_NONBLOCKING;
    std::array<float, kLaneCount> laneValues() const noexcept CL_NONBLOCKING;
    std::uint64_t activeNotesLowMask() const noexcept CL_NONBLOCKING;
    std::uint64_t activeNotesHighMask() const noexcept CL_NONBLOCKING;
    int lastBlockEventCount() const noexcept CL_NONBLOCKING;
    bool isPlaying() const noexcept CL_NONBLOCKING;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cl
