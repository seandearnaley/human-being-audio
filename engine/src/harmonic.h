// The harmonic core's pitch-tracked pieces (spec, "Revision 0.2"):
//   Reach   a 4th-order Butterworth low-pass at reach x f0 after the fold, per
//           voice; the feedback loop gets the same filter at min(reach, 8) x f0;
//   Weight  extra slope, from 0 to -12 dB/oct, by blending dry -> one one-pole
//           -> two one-poles (corner two octaves above a pivot), level-matched
//           over the octave above the pivot so it leans the spectrum instead of
//           lowering it;
//   Sculpt  a pitch-tracked bank of bells after Reach and Weight;
//   Fit     the phase the sub's LR4 low-pass takes away at f0, which the sub
//           oscillator runs ahead by.
// All of it runs at control rate (coefficients) and the oversampled rate (filters).
#pragma once

#include "crossover.h"
#include "svf.h"

#include <array>
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

// ---- Reach -----------------------------------------------------------------

// 4th-order Butterworth as two SVF sections: k = 2 cos(pi/8) and 2 cos(3 pi/8)
// (Q 0.54 and 1.31). -3 dB at the cutoff, flat below it, 24 dB/oct above.
inline constexpr double kButterworth4K1 = 1.8477590650225735;
inline constexpr double kButterworth4K2 = 0.7653668647301796;

// The feedback loop's low-pass sits at min(reach, this) x f0: the loop's
// stability depends on how many harmonics it feeds back, so Reach opening the
// voices up does not open the loop past this.
inline constexpr double kLoopMaxHarmonic = 8.0;

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

// ---- Weight ----------------------------------------------------------------

// Weight's pivot: 3 f0, but never below 1.5 x the sub low-pass. Below that the
// growl is high-passed away (it is the sub's band), so a pivot there would only
// turn the audible growl down.
inline double weightPivotHz(double f0, double subFreq) noexcept TF_NONBLOCKING {
    return std::max(3.0 * f0, 1.5 * subFreq);
}

// Weight's coefficients: the one-poles' TPT gain and the blend of dry, one
// pole and two poles, level compensation included.
struct WeightMix {
    double gain = 0.0;  // G = g / (1 + g)
    double dry = 1.0, one = 0.0, two = 0.0;
};

// The one-poles' corner sits this many times above the pivot. With the corner
// at the pivot itself, two poles fall 8 dB across the octave above it, and the
// default patch's growl in that octave (weighted to its top) dropped 3.8 dB
// at weight 1; two octaves up the octave stays within 0.7 dB.
inline constexpr double kWeightCornerRatio = 4.0;

// The extra slope (feel::weightSlopeDb, 0 to -12 dB/oct) is made by blending
// dry -> one one-pole -> two cascaded one-poles at kWeightCornerRatio x the
// pivot, each pole worth -6 dB/oct far above it: -3 dB/oct is half dry, half
// one pole (a shelf rather than a true half-order slope). Normalisation: the
// blend's power gain, averaged over five points spanning the octave above the
// pivot (exactly, for the digital filters at `sampleRate`), is brought back to
// 1, so that octave keeps its level and the control tilts the spectrum about
// it. At 0 dB/oct the blend is exactly the dry signal.
inline WeightMix weightBlend(double slopeDbPerOct, double pivotHz, double sampleRate) noexcept TF_NONBLOCKING {
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
    const double pivot = std::min(pivotHz, kMaxFilterRatio * sampleRate);
    const double g = SvfCoefs::prewarp(std::min(kWeightCornerRatio * pivot, kMaxFilterRatio * sampleRate), sampleRate);
    m.gain = g / (1.0 + g);
    // One pole: H = 1 / (1 + jW) = (1 - jW) / (1 + W^2); two: H^2.
    double power = 0.0;
    constexpr int kPoints = 5;
    for (int i = 0; i < kPoints; ++i) {
        const double hz = std::min(pivot * std::exp2(static_cast<double>(i) / (kPoints - 1)), 0.499 * sampleRate);
        const double wr = std::tan(kPi * hz / sampleRate) / g;
        const double d = 1.0 + wr * wr;
        const double re = m.dry + m.one / d + m.two * (1.0 - wr * wr) / (d * d);
        const double im = -m.one * wr / d - m.two * 2.0 * wr / (d * d);
        power += (re * re + im * im) / kPoints;
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

// ---- Sculpt ----------------------------------------------------------------

inline constexpr int kSculptMaxBands = 6;

// Each band is a bell made from a TPT state-variable filter (Simper's
// "linear trap" SVF, the same one as the tract): y = x + (G - 1) k v1, with v1
// the band output, k = 1/Q fixed and G the linear gain at the centre. The
// band-pass k v1 is exactly 1 at the centre, so the centre gain is G however
// deep, and with k independent of G a -30 dB notch stays as narrow as a
// +15 dB peak (two octaves off a Q 6 band moves the level by under 0.5 dB).
// g, k and m = (G - 1) k move linearly per sample between control points and
// a1..a3 are recomputed from them, which the TPT form takes without clicks. A
// band whose m is exactly 0 is skipped and its state cleared, so depth 0 is
// bit-identical to no stage at all.
class SculptCoefs {
public:
    void setTarget(int band, double g, double k, double m, int steps) noexcept TF_NONBLOCKING {
        auto& b = bands_[static_cast<std::size_t>(band)];
        // A target repeated from the previous control point is reached
        // exactly, so a band that fades to m = 0 really switches off.
        const auto move = [steps](Ramp& r, double& last, double end) {
            if (steps <= 0 || end == last) r.snap(end);
            else r.to(end, steps);
            last = end;
        };
        move(b.g, b.lastG, g);
        move(b.k, b.lastK, k);
        move(b.m, b.lastM, m);
        refresh(b);
    }
    void advance() noexcept TF_NONBLOCKING {
        for (auto& b : bands_) {
            if (!active(b)) continue;
            b.g.next();
            b.k.next();
            b.m.next();
            refresh(b);
        }
    }
    bool active(int band) const noexcept TF_NONBLOCKING { return active(bands_[static_cast<std::size_t>(band)]); }
    const SvfCoefs& coefs(int band) const noexcept TF_NONBLOCKING { return bands_[static_cast<std::size_t>(band)].coefs; }
    double mix(int band) const noexcept TF_NONBLOCKING { return bands_[static_cast<std::size_t>(band)].m.value; }
    bool any() const noexcept TF_NONBLOCKING {
        for (const auto& b : bands_)
            if (active(b)) return true;
        return false;
    }

private:
    struct Band {
        Ramp g, k, m;
        double lastG = -1.0, lastK = -1.0, lastM = -1.0;
        SvfCoefs coefs;
    };
    static bool active(const Band& b) noexcept TF_NONBLOCKING { return b.m.value != 0.0 || b.m.increment != 0.0; }
    static void refresh(Band& b) noexcept TF_NONBLOCKING {
        if (active(b)) b.coefs = SvfCoefs::fromGK(b.g.value, b.k.value);
    }
    std::array<Band, kSculptMaxBands> bands_ {};
};

class SculptChannel {
public:
    void reset() noexcept TF_NONBLOCKING {
        for (auto& s : state_) s.reset();
    }
    double process(const SculptCoefs& c, double x) noexcept TF_NONBLOCKING {
        for (int i = 0; i < kSculptMaxBands; ++i) {
            auto& s = state_[static_cast<std::size_t>(i)];
            if (!c.active(i)) {
                s.reset();
                continue;
            }
            double v1, v2;
            s.tick(c.coefs(i), x, v1, v2);
            x += c.mix(i) * v1;
        }
        return x;
    }

private:
    std::array<SvfState, kSculptMaxBands> state_ {};
};

}
