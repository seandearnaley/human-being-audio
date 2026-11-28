// SPDX-License-Identifier: GPL-3.0-or-later
// The cable, one way only (nothing comes back into the plugin). It connects out to the hub the
// Stage app runs on loopback and writes one JSON line per block, tagged with this instance's id
// and role, so several tracks can feed one Stage. If sound reaches the plugin's input it goes to
// the hub too, on a second connection, as raw samples.
#pragma once

#include "Capture.h"

#include <memory>

namespace stage {

class Link {
  public:
    Link(Capture& capture, SampleRing& samples);
    ~Link();
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    // 0 is the drums; 1 to 4 are layers; 5 is the song's sound.
    void setRole(int role);
    void setSampleRate(double rate);
    [[nodiscard]] bool hubConnected() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace stage
