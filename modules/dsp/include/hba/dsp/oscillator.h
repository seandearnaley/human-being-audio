// SPDX-License-Identifier: GPL-3.0-or-later
// Band-limited PolyBLEP oscillator primitives (zero-allocation, [[clang::nonblocking]]).
#pragma once

#include <hba/dsp/nonblocking.h>

#include <algorithm>

namespace hba::dsp {

inline double polyBlep(double phase, double inc) noexcept HBA_NONBLOCKING {
    if (inc <= 0.0)
        return 0.0;
    if (phase < inc) {
        const double x = phase / inc;
        return x + x - x * x - 1.0;
    }
    if (phase > 1.0 - inc) {
        const double x = (phase - 1.0) / inc;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}

inline double polyBlepSaw(double phase, double inc) noexcept HBA_NONBLOCKING {
    return (2.0 * phase - 1.0) - polyBlep(phase, inc);
}

inline double polyBlepPulse(double phase, double inc, double width) noexcept HBA_NONBLOCKING {
    const double pw = std::clamp(width, 0.05, 0.95);
    double y = phase < pw ? 1.0 : -1.0;
    y += polyBlep(phase, inc);
    double p2 = phase - pw;
    if (p2 < 0.0)
        p2 += 1.0;
    y -= polyBlep(p2, inc);
    return y;
}

inline double polyBlepSquare(double phase, double inc) noexcept HBA_NONBLOCKING {
    return polyBlepPulse(phase, inc, 0.5);
}

} // namespace hba::dsp
