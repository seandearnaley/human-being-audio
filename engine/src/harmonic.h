// The harmonic core's pitch-tracked pieces (spec, "Revision 0.2"):
//   Reach   a 4th-order Butterworth low-pass at reach x f0 after the fold, per
//           voice, and the same filter on the feedback loop;
//   Weight  extra slope above 3 f0, from 0 to -12 dB/oct, by blending dry ->
//           one one-pole -> two one-poles at 3 f0, level-matched so the 2nd and
//           3rd harmonics keep their level and energy moves down rather than
//           just away;
//   Fit     the phase the sub's LR4 low-pass takes away at f0, which the sub
//           oscillator runs ahead by.
// All of it runs at control rate (coefficients) and the oversampled rate (filters).
#pragma once

#include "crossover.h"
#include "svf.h"

#include <cmath>

namespace tf::dsp {

// Pitch-tracked cutoffs stay below this fraction of the rate they run at.
inline constexpr double kMaxFilterRatio = 0.45;

// x wrapped into [-0.5, 0.5): the shortest way round, in cycles.
inline double wrapHalf(double x) noexcept TF_NONBLOCKING { return x - std::floor(x + 0.5); }

// Phase lag, in cycles, of the engine's LR4 low-pass (two TPT Butterworth
// sections at `cutoff`, running at `sampleRate`) at `hz`. The TPT section is the
// bilinear transform of 1 / (s^2 + sqrt2 s + 1) with the cutoff pre-warped, so
// at hz its response is the analogue one at W = tan(pi hz / fs) / tan(pi fc / fs):
// each section lags atan2(sqrt2 W, 1 - W^2).
inline double lr4LowpassLagCycles(double hz, double cutoff, double sampleRate) noexcept TF_NONBLOCKING {
    if (hz <= 0.0 || hz >= 0.499 * sampleRate) return 0.0;
    const double w = std::tan(kPi * hz / sampleRate) / SvfCoefs::prewarp(cutoff, sampleRate);
    const double section = std::atan2(1.0 / kButterworthQ * w, 1.0 - w * w);
    return 2.0 * section / kTwoPi;
}

// 4th-order Butterworth as two SVF sections: k = 2 cos(pi/8) and 2 cos(3 pi/8)
// (Q 0.54 and 1.31). -3 dB at the cutoff, flat below it, 24 dB/oct above.
inline constexpr double kButterworth4K1 = 1.8477590650225735;
inline constexpr double kButterworth4K2 = 0.7653668647301796;

struct ReachCoefs {
    SvfCoefs first, second;
    static ReachCoefs make(double g) noexcept TF_NONBLOCKING {
        return { SvfCoefs::fromGK(g, kButterworth4K1), SvfCoefs::fromGK(g, kButterworth4K2) };
    }
};

class ReachFilter {
public:
    void reset() noexcept TF_NONBLOCKING {
        a_.reset();
        b_.reset();
    }
    double process(const ReachCoefs& c, double x) noexcept TF_NONBLOCKING {
        return b_.lowpass(c.second, a_.lowpass(c.first, x));
    }

private:
    SvfState a_, b_;
};

// Weight's coefficients: the one-poles' TPT gain and the blend of dry, one
// pole and two poles, level compensation included.
struct WeightMix {
    double gain = 0.0;  // G = g / (1 + g)
    double dry = 1.0, one = 0.0, two = 0.0;
};

// The extra slope above 3 f0 (feel::weightSlopeDb, 0 to -12 dB/oct) is made by
// blending dry -> one one-pole -> two cascaded one-poles at 3 f0, each pole
// worth -6 dB/oct far above it: -3 dB/oct is half dry, half one pole (a shelf
// rather than a true half-order slope). Normalisation: the blend's power gain,
// averaged over the 2nd and 3rd harmonics (exactly, for the digital filters at
// `sampleRate`), is brought back to 1, so the control moves energy down the
// spectrum instead of turning the growl down. At 0 dB/oct the blend is
// exactly the dry signal.
inline WeightMix weightBlend(double slopeDbPerOct, double f0, double sampleRate) noexcept TF_NONBLOCKING {
    constexpr double kPoleSlopeDb = 6.0;  // dB/oct of one pole, far above its corner
    const double poles = std::clamp(-slopeDbPerOct / kPoleSlopeDb, 0.0, 2.0);
    WeightMix m;
    if (poles <= 1.0) {
        m.dry = 1.0 - poles;
        m.one = poles;
    } else {
        m.dry = 0.0;
        m.one = 2.0 - poles;
        m.two = poles - 1.0;
    }
    const double g = SvfCoefs::prewarp(std::min(3.0 * f0, kMaxFilterRatio * sampleRate), sampleRate);
    m.gain = g / (1.0 + g);
    // One pole: H = 1 / (1 + jW) = (1 - jW) / (1 + W^2); two: H^2.
    double power = 0.0;
    for (const double harmonic : { 2.0, 3.0 }) {
        const double hz = std::min(harmonic * f0, 0.499 * sampleRate);
        const double wr = std::tan(kPi * hz / sampleRate) / g;
        const double d = 1.0 + wr * wr;
        const double re = m.dry + m.one / d + m.two * (1.0 - wr * wr) / (d * d);
        const double im = -m.one * wr / d - m.two * 2.0 * wr / (d * d);
        power += 0.5 * (re * re + im * im);
    }
    const double compensation = 1.0 / std::sqrt(std::max(power, 1e-12));
    m.dry *= compensation;
    m.one *= compensation;
    m.two *= compensation;
    return m;
}

class WeightFilter {
public:
    void reset() noexcept TF_NONBLOCKING { s1_ = s2_ = 0.0; }
    double process(const WeightMix& m, double x) noexcept TF_NONBLOCKING {
        const double v1 = (x - s1_) * m.gain;
        const double lp1 = v1 + s1_;
        s1_ = lp1 + v1;
        const double v2 = (lp1 - s2_) * m.gain;
        const double lp2 = v2 + s2_;
        s2_ = lp2 + v2;
        return m.dry * x + m.one * lp1 + m.two * lp2;
    }

private:
    double s1_ = 0.0, s2_ = 0.0;
};

}
