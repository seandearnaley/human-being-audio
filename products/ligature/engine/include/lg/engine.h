// SPDX-License-Identifier: AGPL-3.0-or-later
// Ligature's real-time performer. Host-locked: every block derives its position from the host's PPQ (or the preview
// clock while the host is stopped), so loop braces, scrubbing and tempo changes keep it on the host's grid.
// Launches and bridges are scheduled at absolute beats. Zero allocation, [[clang::nonblocking]] verified.
#pragma once

#include "lg/compiled.h"
#include "lg/nonblocking.h"
#include "lg/triple_buffer.h"
#include "lg/voice_renderer.h"

#include <array>
#include <cstdint>
#include <memory>

namespace lg {

struct Transport {
    double bpm = 122.0;
    double ppq = 0.0;
    bool playing = false;
};

inline constexpr int kMaxMidiOut = 1024;

struct MidiOutEvent {
    int sampleOffset = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 3> data{};
};

struct MidiOutBuffer {
    std::array<MidiOutEvent, kMaxMidiOut> events{};
    int count = 0;
    void clear() noexcept LG_NONBLOCKING { count = 0; }
    bool push(int sampleOffset, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2) noexcept LG_NONBLOCKING {
        if (count >= kMaxMidiOut)
            return false;
        auto& e = events[static_cast<std::size_t>(count++)];
        e.sampleOffset = sampleOffset;
        e.size = 3;
        e.data = {b0, b1, b2};
        return true;
    }
};

struct EngineParams {
    int landBars = 4;          // 1 (next bar), 2, or 4 (phrase)
    bool audition = true;      // render the sketch synth
    float auditionGain = 0.8f; // linear
    bool beat = true;          // audition drums under the parts
    int outputRole = -1;       // -1 leads: all parts on channels 1-4. 0..3 follows: that part alone, on channel 1.
    bool studioVoices = false; // play through the installed VoiceRenderer (the studio's instruments) when there is one
};

enum class CommandKind : std::uint8_t {
    launch,       // index: section. Lands on the Land grid through its bridge.
    launchDirect, // index: section. Lands on the Land grid with no bridge.
    fire,         // index: bridge. Tap: lands on the Land grid.
    hold,         // index: bridge. Starts on the next bar and loops its first bar until release.
    release,      // ends a hold: the bridge plays on and lands
    previewPlay,  // index: item to start from (section index, or 100 + bridge index)
    previewStop,
    audition, // while stopped: index item (as previewPlay), span: the chord to sound for a second
};

struct Command {
    CommandKind kind = CommandKind::launch;
    std::int16_t index = 0;
    std::int16_t span = 0;
};

inline constexpr std::int16_t kPreviewBridgeBase = 100;

// The part of the performance a follower copies from its leader.
struct Performance {
    ItemRef item;
    double anchor = 0.0; // beat where the playing item's current pass began
    int fit = 0;         // bars the playing bridge was squeezed to (0 = full)
    int target = -1;     // section a playing bridge lands on
    bool hold = false;
    bool planActive = false;
    int planBridge = -1; // -1: a direct launch
    int planTarget = -1;
    double planStart = 0.0; // beat the bridge (or the direct launch) starts
    int planFit = 0;
    bool started = false;
    // The leader's preview clock, refreshed every block without counting as a change: followers preview with it.
    bool previewing = false;
    double previewBeat = 0.0;
    std::uint64_t generation = 0;
};

struct EngineTelemetry {
    bool hostPlaying = false;
    bool previewPlaying = false;
    bool playing = false;
    double beat = 0.0; // the clock at the start of the last block
    double bpm = 122.0;
    Performance performance;
    int playingBars = 4;     // the playing item's length (a squeezed bridge reports its squeezed length)
    float stepInItem = 0.0f; // 0 .. playingBars * 16
    int barsToStart = -1;    // bars until a planned bridge or launch starts, -1 when nothing is planned
    int barsToLand = -1;     // bars until the target section lands
    std::array<std::array<std::uint64_t, 2>, kRoleCount> sounding{}; // 128-bit note masks per part
    float peak = 0.0f;
    std::uint64_t songRevision = 0;
};

class Engine {
  public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept LG_NONBLOCKING;

    // Message thread: compile into songBuffer().write(), then songBuffer().publish(). The audio thread picks the new
    // song up at its next block and re-reads it from the same timeline position.
    TripleBuffer<CompiledSong>& songBuffer() noexcept { return *songs_; }

    // Message thread, before playback: the studio voices. Not owned; must outlive the engine's use of it.
    void setVoiceRenderer(VoiceRenderer* voices) noexcept;

    // Audio thread (the processor drains its command FIFO into this at the start of each block).
    void command(const Command& c) noexcept LG_NONBLOCKING;
    // Audio thread: a follower adopts its leader's performance before process().
    void follow(const Performance& p) noexcept LG_NONBLOCKING;
    Performance performance() const noexcept LG_NONBLOCKING;

    // Writes sample-accurate MIDI into `out` (cleared first) and, when `left`/`right` are not null, the sketch synth.
    void process(const EngineParams& params, const Transport& host, MidiOutBuffer& out, float* left, float* right,
                 int numSamples) noexcept LG_NONBLOCKING;

    // One-key chords (audio thread, before process()): white keys C4-B5 (MIDI 60-83) play the playing section's own
    // chords on the chords channel; other notes are ignored. `sampleOffset` is the event's position in the coming
    // block.
    void inputNote(int note, int velocity, bool on, int sampleOffset) noexcept LG_NONBLOCKING;

    // Bypass or stop: a note-off for every sounding note at sample 0.
    void releaseAll(MidiOutBuffer& out) noexcept LG_NONBLOCKING;

    EngineTelemetry telemetry() const noexcept LG_NONBLOCKING;

  private:
    struct Impl;
    std::unique_ptr<TripleBuffer<CompiledSong>> songs_;
    std::unique_ptr<Impl> impl_;
    VoiceRenderer* voices_ = nullptr;
};

} // namespace lg
