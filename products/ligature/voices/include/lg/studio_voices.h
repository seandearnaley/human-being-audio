// SPDX-License-Identifier: AGPL-3.0-or-later
// Studio voices: Ligature's parts played through the studio's own instruments instead of the sketch synth.
//   chords  Torus Glide, "Live Capture Blank Slate" (Copedent, held notes spread over its five voices)
//   bass    Lineage, "Velvet Analog Reese"
//   melody  Torus Glide, "MPE Slide Manual" (a pulse lead in unison)
//   arp     Strike Body, "Glass Bell Perc" (chromatic modal pluck)
//   kick    Datum, "Twin-T 909 Club" (its sequencer and reference bass off)
//   clap    Strike Body, "Layered Neuro Clap"
//   hat     a small noise-and-metal hat of its own
// Each voice is trimmed to sit at the sketch synth's level for its part. Allocation happens only in prepare();
// every other call is [[clang::nonblocking]].
#pragma once

#include "lg/voice_renderer.h"

#include <memory>

namespace lg {

class StudioVoices final : public VoiceRenderer {
  public:
    StudioVoices();
    ~StudioVoices() override;
    StudioVoices(const StudioVoices&) = delete;
    StudioVoices& operator=(const StudioVoices&) = delete;

    // Message thread, before playback: builds every instrument from its embedded factory preset.
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept LG_NONBLOCKING;

    // Samples the studio voices sound behind the notes that trigger them: the instruments' look-ahead limiters,
    // aligned to the slowest so the parts stay together. A host can report this as the plugin's latency.
    int latencySamples() const noexcept LG_NONBLOCKING;
    // False until prepare() has loaded every embedded preset; the voices stay silent until then.
    bool presetsLoaded() const noexcept LG_NONBLOCKING;

    void noteOn(int role, int pitch, float velocity) noexcept LG_NONBLOCKING override;
    void noteOff(int role, int pitch) noexcept LG_NONBLOCKING override;
    void drum(int kind, float velocity) noexcept LG_NONBLOCKING override;
    void releaseAll() noexcept LG_NONBLOCKING override;
    void render(float* left, float* right, int count, float gain, double bpm, double beat,
                bool playing) noexcept LG_NONBLOCKING override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lg
