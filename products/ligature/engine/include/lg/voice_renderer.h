// SPDX-License-Identifier: AGPL-3.0-or-later
// A sound source the engine can play its parts through instead of the sketch synth. The plugin installs one built
// from the studio's own instruments (lg_voices); the engine and its tests never depend on it.
#pragma once

#include "lg/nonblocking.h"

namespace lg {

class VoiceRenderer {
  public:
    virtual ~VoiceRenderer() = default;
    // role: 0 chords, 1 bass, 2 melody, 3 arp. drum kind: 0 kick, 1 hat, 2 clap.
    virtual void noteOn(int role, int pitch, float velocity) noexcept LG_NONBLOCKING = 0;
    virtual void noteOff(int role, int pitch) noexcept LG_NONBLOCKING = 0;
    virtual void drum(int kind, float velocity) noexcept LG_NONBLOCKING = 0;
    virtual void releaseAll() noexcept LG_NONBLOCKING = 0;
    // Adds `count` samples into left/right. bpm/beat/playing describe the clock at the first sample.
    virtual void render(float* left, float* right, int count, float gain, double bpm, double beat,
                        bool playing) noexcept LG_NONBLOCKING = 0;
};

} // namespace lg
