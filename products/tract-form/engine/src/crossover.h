#pragma once
#include "svf.h"
#include "tf/nonblocking.h"
#include <hba/dsp/crossover.h>
#include <array>

namespace tf::dsp {
using hba::dsp::Lr4Allpass;
using hba::dsp::Lr4Highpass;
using hba::dsp::Lr4Lowpass;
using hba::dsp::Lr4Split;
using hba::dsp::ThreeBandSplit;
using hba::dsp::butterworth;

// 4th-order Butterworth high-pass (2 cascaded TPT SVF sections: Q 0.54 and 1.31).
// Two passes across the 2x growl clipper form an 8th-order Linkwitz-Riley (-48 dB/oct,
// -64 dB at f0) with gentle transient ringing so the second clip pass cleanly trims
// overshoot without carving a -30 dB cavity at the split.
struct Bw4Coefs {
    SvfCoefs first, second;

    static Bw4Coefs make(double hz, double sampleRate) noexcept TF_NONBLOCKING {
        const double g = SvfCoefs::prewarp(hz, sampleRate);
        return {SvfCoefs::fromGK(g, 1.8477590650225735), SvfCoefs::fromGK(g, 0.7653668647301796)};
    }
};

class Bw4Highpass {
  public:
    void reset() noexcept TF_NONBLOCKING {
        a_.reset();
        b_.reset();
    }
    double process(const Bw4Coefs& c, double x) noexcept TF_NONBLOCKING {
        return b_.highpass(c.second, a_.highpass(c.first, x));
    }

  private:
    SvfState a_, b_;
};
}
