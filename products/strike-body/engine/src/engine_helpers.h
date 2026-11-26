// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/params.h"

#include <hba/dsp/nonblocking.h>

#include <cmath>

namespace sb::detail {

inline double semitoneRatio(double st) noexcept HBA_NONBLOCKING { return std::pow(2.0, st / 12.0); }

struct ArtProfile {
    double bodyGain = 1.0;
    double bodyDecayMult = 1.0;
    double shellGain = 1.0;
    double shellPitchSt = 0.0;
    double strikeShift = 0.0;
    double wireGain = 1.0;
    double wireDecayMult = 1.0;
    double clickGain = 1.0;
};

inline ArtProfile profileFor(Articulation art) noexcept HBA_NONBLOCKING {
    switch (art) {
    case Articulation::main:
        return {1.00, 1.00, 1.00, 0.0, 0.00, 1.00, 1.00, 1.00};
    case Articulation::ghost:
        return {0.50, 0.56, 0.64, 1.4, 0.14, 0.72, 0.60, 0.42};
    case Articulation::rim:
        return {0.26, 0.44, 1.30, 4.5, 0.48, 0.42, 0.50, 1.38};
    case Articulation::roll:
        return {0.78, 0.60, 0.90, 1.0, 0.18, 0.84, 0.54, 1.16};
    }
    return {};
}

// Compute effective macro-modulated decay multiplier from bipolar macroTight (-1..+1).
inline double tightDecayScale(double macroTight) noexcept HBA_NONBLOCKING {
    // +1.0 -> 0.34x (ultra-tight 174 BPM snap), 0.0 -> 1.0x, -1.0 -> 2.35x (booming halftime!)
    return std::pow(2.45, -macroTight);
}

} // namespace sb::detail
