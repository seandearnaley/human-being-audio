// Linkwitz-Riley 4th-order (LR4) filters built from two Butterworth SVF
// sections. An LR4 low-pass and high-pass at the same frequency are in phase and
// sum to the 2nd-order all-pass (s^2 - k s + 1) / (s^2 + k s + 1), k = sqrt 2.
#pragma once

#include "svf.h"

namespace tf::dsp {

inline SvfCoefs butterworth(double hz, double sampleRate) noexcept TF_NONBLOCKING {
    return SvfCoefs::make(hz, kButterworthQ, sampleRate);
}

class Lr4Lowpass {
public:
    void reset() noexcept TF_NONBLOCKING { a_.reset(); b_.reset(); }
    double process(const SvfCoefs& c, double x) noexcept TF_NONBLOCKING { return b_.lowpass(c, a_.lowpass(c, x)); }

private:
    SvfState a_, b_;
};

class Lr4Highpass {
public:
    void reset() noexcept TF_NONBLOCKING { a_.reset(); b_.reset(); }
    double process(const SvfCoefs& c, double x) noexcept TF_NONBLOCKING { return b_.highpass(c, a_.highpass(c, x)); }

private:
    SvfState a_, b_;
};

// The all-pass that an LR4 pair at the same frequency sums to.
class Lr4Allpass {
public:
    void reset() noexcept TF_NONBLOCKING { a_.reset(); }
    double process(const SvfCoefs& c, double x) noexcept TF_NONBLOCKING { return a_.allpass(c, x); }

private:
    SvfState a_;
};

// One LR4 split: the first section is shared by both outputs.
class Lr4Split {
public:
    void reset() noexcept TF_NONBLOCKING { first_.reset(); low_.reset(); high_.reset(); }
    void process(const SvfCoefs& c, double x, double& low, double& high) noexcept TF_NONBLOCKING {
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
    void reset() noexcept TF_NONBLOCKING { lo_.reset(); hi_.reset(); comp_.reset(); }
    void process(const SvfCoefs& cLo, const SvfCoefs& cHi, double x, double& low, double& mid,
                 double& high) noexcept TF_NONBLOCKING {
        double rest;
        lo_.process(cLo, x, low, rest);
        hi_.process(cHi, rest, mid, high);
        low = comp_.process(cHi, low);
    }

private:
    Lr4Split lo_, hi_;
    Lr4Allpass comp_;
};

}
