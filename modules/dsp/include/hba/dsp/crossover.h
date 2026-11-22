// SPDX-License-Identifier: GPL-3.0-or-later
// Linkwitz-Riley 4th-order (LR4) filters built from two Butterworth SVF
// sections, plus a two-stage 1st-order all-pass phase rotator.
#pragma once

#include <hba/dsp/svf.h>

#include <algorithm>
#include <cmath>

namespace hba::dsp {

inline SvfCoefs butterworth(double hz, double sampleRate) noexcept HBA_NONBLOCKING {
    return SvfCoefs::make(hz, kButterworthQ, sampleRate);
}

class Lr4Lowpass {
public:
    void reset() noexcept HBA_NONBLOCKING { a_.reset(); b_.reset(); }
    double process(const SvfCoefs& c, double x) noexcept HBA_NONBLOCKING { return b_.lowpass(c, a_.lowpass(c, x)); }

private:
    SvfState a_, b_;
};

class Lr4Highpass {
public:
    void reset() noexcept HBA_NONBLOCKING { a_.reset(); b_.reset(); }
    double process(const SvfCoefs& c, double x) noexcept HBA_NONBLOCKING { return b_.highpass(c, a_.highpass(c, x)); }

private:
    SvfState a_, b_;
};

// The all-pass that an LR4 pair at the same frequency sums to.
class Lr4Allpass {
public:
    void reset() noexcept HBA_NONBLOCKING { a_.reset(); }
    double process(const SvfCoefs& c, double x) noexcept HBA_NONBLOCKING { return a_.allpass(c, x); }

private:
    SvfState a_;
};

// One LR4 split: the first section is shared by both outputs.
class Lr4Split {
public:
    void reset() noexcept HBA_NONBLOCKING { first_.reset(); low_.reset(); high_.reset(); }
    void process(const SvfCoefs& c, double x, double& low, double& high) noexcept HBA_NONBLOCKING {
        double v1, v2;
        first_.tick(c, x, v1, v2);
        low = low_.lowpass(c, v2);
        high = high_.highpass(c, x - c.k * v1 - v2);
    }

private:
    SvfState first_, low_, high_;
};

// Three bands that sum to an all-pass: low = AP_hi(LP_lo(x)), mid = LP_hi(HP_lo(x)),
// high = HP_hi(HP_lo(x)), so low + mid + high = AP_hi(AP_lo(x)).
class ThreeBandSplit {
public:
    void reset() noexcept HBA_NONBLOCKING { lo_.reset(); hi_.reset(); comp_.reset(); }
    void process(const SvfCoefs& cLo, const SvfCoefs& cHi, double x, double& low, double& mid,
                 double& high) noexcept HBA_NONBLOCKING {
        double rest;
        lo_.process(cLo, x, low, rest);
        hi_.process(cHi, rest, mid, high);
        low = comp_.process(cHi, low);
    }

private:
    Lr4Split lo_, hi_;
    Lr4Allpass comp_;
};

// Two cascaded 1st-order all-pass sections sweeping phase smoothly 0..360 deg
// around the sub crossover without altering magnitude.
class PhaseRotator {
public:
    void reset() noexcept HBA_NONBLOCKING { z1_ = z2_ = 0.0; }
    double process(double x, double degrees) noexcept HBA_NONBLOCKING {
        if (degrees <= 0.01) return x;
        const double rad = degrees * (kPi / 180.0);
        const double half = std::clamp(0.5 * rad, 0.0, kPi - 0.02);
        const double a = std::clamp((std::tan(0.5 * half) - 1.0) / (std::tan(0.5 * half) + 1.0), -0.96, 0.96);
        const double y1 = -a * x + z1_;
        z1_ = x + a * y1;
        const double y2 = -a * y1 + z2_;
        z2_ = y1 + a * y2;
        return y2;
    }

private:
    double z1_ = 0.0;
    double z2_ = 0.0;
};

}  // namespace hba::dsp
