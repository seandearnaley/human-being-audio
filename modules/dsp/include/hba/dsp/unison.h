// SPDX-License-Identifier: GPL-3.0-or-later
// Unison stacks: detune offset tables, equal-power voice gains and deterministic
// start phases. The oscillator itself stays with the product; this is the
// bookkeeping every stacked voice shares.
#pragma once

#include <hba/dsp/nonblocking.h>
#include <hba/dsp/random.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hba::dsp {

inline constexpr int kMaxUnison = 7;

// Detune offsets in units of the stack's spread, centre first, then outwards.
// A stack of n voices plays the first n entries.
using UnisonOffsets = std::array<double, kMaxUnison>;

// Adam Szabo's measurement of the Roland JP-8000 super saw ("How to Emulate the
// Super Saw", KTH 2010, as transcribed; the thesis itself was not reachable),
// normalised to the widest voice. The spacing is asymmetric, so no two pairs
// beat at the same rate.
inline constexpr UnisonOffsets kSzaboOffsets{0.0, 0.1809, -0.1773, 0.5655, -0.5718, 0.9773, -1.0};

// Symmetric golden-ratio spacing, the layout Lineage's reese core plays.
inline constexpr UnisonOffsets kSymmetricOffsets{0.0, 0.275, -0.275, 0.618, -0.618, 1.0, -1.0};

struct UnisonGains {
    double centre = 1.0;
    double side = 0.0;
};

// Gains for a stack of `count` voices whose sides sit at `sideLevel` of the
// centre, scaled so the stack has the centre voice's power when the voices are
// uncorrelated. A stack of one is the bare voice at unity gain.
inline UnisonGains unisonGains(int count, double sideLevel) noexcept HBA_NONBLOCKING {
    const int n = std::clamp(count, 1, kMaxUnison);
    const double side = std::clamp(sideLevel, 0.0, 1.0);
    if (n == 1)
        return {1.0, 0.0};
    const double norm = 1.0 / std::sqrt(1.0 + side * side * static_cast<double>(n - 1));
    return {norm, side * norm};
}

// Start phases in cycles: the centre voice at 0, the sides spread by a PRNG
// seeded with `seed`, so the same seed always gives the same stack.
inline void unisonStartPhases(std::uint32_t seed, std::array<double, kMaxUnison>& phases) noexcept HBA_NONBLOCKING {
    XorShift32 rng(seed * 2654435761u + 0x6A09E667u);
    phases[0] = 0.0;
    for (std::size_t i = 1; i < phases.size(); ++i)
        phases[i] = rng.nextUnit();
}

// Fixed start phases in cycles: the centre at 0 and side voice j at
// frac(offset + j / golden ratio), which spreads any number of voices evenly
// and is the same on every call.
inline void unisonSpreadPhases(double offset, std::array<double, kMaxUnison>& phases) noexcept HBA_NONBLOCKING {
    constexpr double kInverseGolden = 0.61803398874989484820;
    phases[0] = 0.0;
    for (std::size_t i = 1; i < phases.size(); ++i) {
        const double p = offset + static_cast<double>(i) * kInverseGolden;
        phases[i] = p - std::floor(p);
    }
}

} // namespace hba::dsp
