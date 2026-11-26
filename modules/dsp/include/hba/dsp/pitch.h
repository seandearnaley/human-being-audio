// SPDX-License-Identifier: GPL-3.0-or-later
// Shared real-time-safe pitch/frequency conversion helpers.
#pragma once

#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <cmath>

namespace hba::dsp {

inline double midiToHz(double midiNote) noexcept HBA_NONBLOCKING { return 440.0 * std::exp2((midiNote - 69.0) / 12.0); }

inline double hzToMidi(double hz) noexcept HBA_NONBLOCKING {
    return 69.0 + 12.0 * std::log2(std::max(1.0e-6, hz) / 440.0);
}

inline double semitonesToRatio(double semitones) noexcept HBA_NONBLOCKING { return std::exp2(semitones / 12.0); }

inline double centsToRatio(double cents) noexcept HBA_NONBLOCKING { return std::exp2(cents / 1200.0); }

} // namespace hba::dsp
