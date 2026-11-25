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
#include "tf/params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

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

// Weight's pivot: 3 f0, but never below 1.5 x the sub/growl split (in Hz,
// subSplit x f0, so 1.5 x subSplit harmonics). Below that the
// growl is high-passed away (it is the sub's band), so a pivot there would only
// turn the audible growl down.
inline double weightPivotHz(double f0, double splitHz) noexcept TF_NONBLOCKING {
    return std::max(3.0 * f0, 1.5 * splitHz);
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
        if (active(b) || band < activeCount_)
            b.coefs = SvfCoefs::fromGK(b.g.value, b.k.value);
    }
    void setSpecial(int activeCount, double depth, double res, double combDelaySamples,
                    int steps) noexcept TF_NONBLOCKING {
        activeCount_ = std::clamp(activeCount, 1, kSculptMaxBands);
        if (steps <= 0) {
            depth_.snap(depth);
            res_.snap(res);
            combDelay_.snap(combDelaySamples);
        } else {
            depth_.to(depth, steps);
            res_.to(res, steps);
            combDelay_.to(combDelaySamples, steps);
        }
    }
    void advance() noexcept TF_NONBLOCKING {
        depth_.next();
        res_.next();
        combDelay_.next();
        for (int i = 0; i < kSculptMaxBands; ++i) {
            auto& b = bands_[static_cast<std::size_t>(i)];
            const bool needCoefs = active(b) || i < activeCount_;
            if (!needCoefs)
                continue;
            b.g.next();
            b.k.next();
            b.m.next();
            b.coefs = SvfCoefs::fromGK(b.g.value, b.k.value);
        }
    }
    bool active(int band) const noexcept TF_NONBLOCKING { return active(bands_[static_cast<std::size_t>(band)]); }
    const SvfCoefs& coefs(int band) const noexcept TF_NONBLOCKING { return bands_[static_cast<std::size_t>(band)].coefs; }
    double mix(int band) const noexcept TF_NONBLOCKING { return bands_[static_cast<std::size_t>(band)].m.value; }
    int activeCount() const noexcept TF_NONBLOCKING { return activeCount_; }
    double depth() const noexcept TF_NONBLOCKING { return depth_.value; }
    double resonance() const noexcept TF_NONBLOCKING { return res_.value; }
    double combDelay() const noexcept TF_NONBLOCKING { return combDelay_.value; }
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
    int activeCount_ = 3;
    Ramp depth_{}, res_{}, combDelay_{};
};

class SculptChannel {
public:
  void prepare(double osSampleRate) {
      // Max comb delay: 30 Hz at oversampled rate (with headroom).
      const int cap = std::max(64, static_cast<int>(std::ceil(osSampleRate / 25.0)) + 16);
      combBuf_.assign(static_cast<std::size_t>(cap), 0.0f);
      combSize_ = cap;
      dampCoef_ = onePoleCoef(0.00018, 1.0 / std::max(osSampleRate, 1000.0));
      reset();
  }

    void reset() noexcept TF_NONBLOCKING {
        for (auto& s : state_) s.reset();
        for (auto& s : phaserState_)
            s.reset();
        phaserFb_ = 0.0;
        combDamp_ = 0.0;
        if (!combBuf_.empty()) {
            int p = combPos_;
            for (int i = 0; i < combWritten_; ++i) {
                p = p == 0 ? combSize_ - 1 : p - 1;
                combBuf_[static_cast<std::size_t>(p)] = 0.0f;
            }
        }
        combPos_ = 0;
        combWritten_ = 0;
    }

    double process(const SculptCoefs& c, double x, SculptMode mode = SculptMode::bell) noexcept TF_NONBLOCKING {
        if (mode == SculptMode::bell) {
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

        const double depth = std::clamp(c.depth(), -1.0, 1.0);
        if (std::abs(depth) <= 1.0e-5) {
            return x;
        }

        if (mode == SculptMode::phaser) {
            const double fbAmt = 0.80 * std::clamp(c.resonance(), 0.0, 1.0) * (depth >= 0.0 ? 1.0 : -1.0);
            double sig = x + fbAmt * std::tanh(phaserFb_);
            const int stages = c.activeCount();
            for (int i = 0; i < kSculptMaxBands; ++i) {
                auto& s = phaserState_[static_cast<std::size_t>(i)];
                if (i >= stages) {
                    s.reset();
                    continue;
                }
                const auto& cf = c.coefs(i);
                double v1, v2;
                s.tick(cf, sig, v1, v2);
                // 2nd-order TPT all-pass: AP(s) = x - 2 * k * v1 (unity magnitude, 360 deg phase sweep).
                sig = sig - 2.0 * cf.k * v1;
            }
            phaserFb_ = sig;
            const double wet = std::abs(depth);
            const double sign = depth >= 0.0 ? 1.0 : -1.0;
            return (1.0 - 0.5 * wet) * x + 0.5 * wet * sign * sig;
        }

        // Comb mode: pitch-tracked fractional-delay feedback comb filter with damped loop.
        if (combSize_ <= 8)
            return x;
        const double maxD = static_cast<double>(combSize_ - 4);
        const double delay = std::clamp(c.combDelay(), 2.0, maxD);
        const int dInt = static_cast<int>(std::floor(delay));
        const double frac = delay - static_cast<double>(dInt);
        const auto readAt = [&](int offset) noexcept TF_NONBLOCKING -> double {
            int idx = combPos_ - offset;
            if (idx < 0)
                idx += combSize_;
            return static_cast<double>(combBuf_[static_cast<std::size_t>(idx)]);
        };
        const double delayed = readAt(dInt) * (1.0 - frac) + readAt(dInt + 1) * frac;
        combDamp_ += dampCoef_ * (delayed - combDamp_);
        const double wet = std::abs(depth);
        const double polarity = depth >= 0.0 ? 1.0 : -1.0;
        const double fb = polarity * (0.20 + 0.68 * std::clamp(c.resonance(), 0.0, 1.0)) * wet;
        const double loopIn = std::tanh(0.65 * (x + fb * combDamp_)) / 0.65;
        combBuf_[static_cast<std::size_t>(combPos_)] = static_cast<float>(loopIn);
        combPos_ = (combPos_ + 1 == combSize_) ? 0 : combPos_ + 1;
        combWritten_ = std::min(combWritten_ + 1, combSize_);
        // Level-balanced comb blend: preserves unity RMS without runaway gain.
        const double norm = 1.0 / (1.0 + 0.45 * wet * std::clamp(c.resonance(), 0.0, 1.0));
        return ((1.0 - 0.5 * wet) * x + 0.5 * wet * loopIn) * norm;
    }

private:
    std::array<SvfState, kSculptMaxBands> state_ {};
    std::array<SvfState, kSculptMaxBands> phaserState_{};
    double phaserFb_ = 0.0;
    std::vector<float> combBuf_;
    int combSize_ = 0;
    int combPos_ = 0;
    int combWritten_ = 0;
    double combDamp_ = 0.0;
    double dampCoef_ = 0.3;
};

}
