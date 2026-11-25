// SPDX-License-Identifier: GPL-3.0-or-later
// Trapezoidal (TPT) state-variable filter after Andrew Simper, Cytomic,
// "Linear trap optimised 2".
#pragma once

#include <hba/dsp/control.h>

#include <algorithm>
#include <cmath>

namespace hba::dsp {

struct SvfCoefs {
    double g = 0.0;  // tan(pi f / fs)
    double k = 1.0;  // 1 / Q
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;

    static SvfCoefs fromGK(double g, double k) noexcept HBA_NONBLOCKING {
        SvfCoefs c;
        c.g = g;
        c.k = k;
        c.a1 = 1.0 / (1.0 + g * (g + k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }
    static double prewarp(double hz, double sampleRate) noexcept HBA_NONBLOCKING {
        const double sr = std::max(sampleRate, 1.0);
        const double safeHz = std::isfinite(hz) ? hz : 1e-3;
        const double f = std::clamp(safeHz, 1e-3, 0.49 * sr);
        return std::tan(kPi * f / sr);
    }
    static SvfCoefs make(double hz, double q, double sampleRate) noexcept HBA_NONBLOCKING {
        const double safeQ = (std::isfinite(q) && q > 1e-6) ? q : 1e-6;
        return fromGK(prewarp(hz, sampleRate), 1.0 / safeQ);
    }
};

inline constexpr double kButterworthQ = 0.70710678118654752440;

struct SvfState {
    double ic1 = 0.0, ic2 = 0.0;

    void reset() noexcept HBA_NONBLOCKING { ic1 = ic2 = 0.0; }

    // Advances one sample; v1 is the band output, v2 the low output.
    void tick(const SvfCoefs& c, double x, double& v1, double& v2) noexcept HBA_NONBLOCKING {
        const double v3 = x - ic2;
        v1 = c.a1 * ic1 + c.a2 * v3;
        v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
    }
    double lowpass(const SvfCoefs& c, double x) noexcept HBA_NONBLOCKING {
        double v1, v2;
        tick(c, x, v1, v2);
        return v2;
    }
    double highpass(const SvfCoefs& c, double x) noexcept HBA_NONBLOCKING {
        double v1, v2;
        tick(c, x, v1, v2);
        return x - c.k * v1 - v2;
    }
    // Band-pass normalised to unity gain at the centre frequency.
    double bandpass(const SvfCoefs& c, double x) noexcept HBA_NONBLOCKING {
        double v1, v2;
        tick(c, x, v1, v2);
        return c.k * v1;
    }
    double allpass(const SvfCoefs& c, double x) noexcept HBA_NONBLOCKING {
        double v1, v2;
        tick(c, x, v1, v2);
        return x - 2.0 * c.k * v1;
    }
};

}  // namespace hba::dsp
