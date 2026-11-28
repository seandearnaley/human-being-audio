// SPDX-License-Identifier: GPL-3.0-or-later
// Exact flat-magnitude matched 1st-order allpass pair for transient phase dispersion.
#pragma once

#include <hba/dsp/control.h>
#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <cmath>

namespace hba::dsp {

// Matched 1st-order allpass pair: H(z) = [(-c + z^-1) / (1 - c * z^-1)]^2.
// Exact |H(e^jw)| == 1.000 at all frequencies, +1 at DC and +1 at Nyquist.
struct AllpassPair {
    double z1 = 0.0;
    double z2 = 0.0;
    double c = 0.0;

    void reset() noexcept HBA_NONBLOCKING { z1 = z2 = 0.0; }

    void setFreq(double hz, double sampleRate) noexcept HBA_NONBLOCKING {
        if (!(sampleRate > 0.0) || !std::isfinite(hz) || !std::isfinite(sampleRate)) {
            c = 0.0;
            return;
        }
        const double maxHz = 0.46 * sampleRate;
        const double f = std::clamp(hz, std::min(20.0, maxHz), maxHz);
        const double t = std::tan(kPi * f / sampleRate);
        c = std::clamp((1.0 - t) / (1.0 + t), -0.985, 0.995);
    }

    double process(double x) noexcept HBA_NONBLOCKING {
        const double y1 = -c * x + z1;
        z1 = x + c * y1;
        const double y2 = -c * y1 + z2;
        z2 = y1 + c * y2;
        return y2;
    }
};

} // namespace hba::dsp
