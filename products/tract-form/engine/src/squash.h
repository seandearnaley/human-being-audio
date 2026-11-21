// Squash: three bands split with LR4 at 200 Hz and 2.5 kHz (all-pass
// compensated, so the bands sum flat), each with upward and downward
// compression from a stereo-linked RMS detector. Make-up is matched to the
// input above the low band: slow RMS detectors on the input and on the
// compressed sum (mid and high bands) give the gain that restores the input
// level, never less than 0 dB, plus a small lift
// that grows with `amount`. So Squash makes any material denser and never
// quieter, whatever its level. `amount` scales thresholds, ratios and the
// upward range; at 0 every gain is exactly 1 and the module is a pure all-pass.
#pragma once

#include "crossover.h"

#include <array>
#include <cmath>

namespace tf::dsp {

inline constexpr int kSquashBands = 3;
inline constexpr double kSquashLowHz = 200.0;
inline constexpr double kSquashHighHz = 2500.0;

// Make-up: RMS-matched over this time, at most this much, plus a lift of
// kSquashLiftDb * amount on top.
inline constexpr double kSquashMakeupSeconds = 0.2;
inline constexpr double kSquashMaxMakeupDb = 24.0;
inline constexpr double kSquashLiftDb = 1.5;

struct SquashCurve {
    // Static gain in dB for a band at `levelDb` and amount `a` (0..1).
    static double gainDb(double levelDb, double a) noexcept TF_NONBLOCKING {
        if (a <= 0.0) return 0.0;
        const double downThreshold = -10.0 - 22.0 * a;
        const double downRatio = 1.0 + 7.0 * a;
        const double upThreshold = -38.0 + 2.0 * a;
        const double upRatio = 1.0 + 2.0 * a;
        const double maxBoost = 18.0 * a;
        if (levelDb > downThreshold) return -(levelDb - downThreshold) * (1.0 - 1.0 / downRatio);
        if (levelDb < upThreshold) {
            double boost = std::min((upThreshold - levelDb) * (1.0 - 1.0 / upRatio), maxBoost);
            // Leave silence and the noise floor alone.
            boost *= std::clamp((levelDb + 90.0) / 20.0, 0.0, 1.0);
            return boost;
        }
        return 0.0;
    }
};

class Squash {
public:
    void prepare(double sampleRate) noexcept TF_NONBLOCKING {
        cLow_ = butterworth(kSquashLowHz, sampleRate);
        cHigh_ = butterworth(kSquashHighHz, sampleRate);
        constexpr std::array<double, kSquashBands> rmsMs { 30.0, 15.0, 8.0 };
        for (std::size_t b = 0; b < kSquashBands; ++b) rmsCoef_[b] = onePoleCoef(rmsMs[b] * 1e-3, 1.0 / sampleRate);
        attackCoef_ = onePoleCoef(0.003, 1.0 / sampleRate);
        releaseCoef_ = onePoleCoef(0.080, 1.0 / sampleRate);
        makeupCoef_ = onePoleCoef(kSquashMakeupSeconds, 1.0 / sampleRate);
        reset();
    }
    void reset() noexcept TF_NONBLOCKING {
        for (auto& s : split_) s.reset();
        meanSquare_.fill(0.0);
        gainDb_.fill(0.0);
        inPower_ = outPower_ = 0.0;
        makeupDb_ = 0.0;
    }

    void process(double& left, double& right, double amount) noexcept TF_NONBLOCKING {
        std::array<double, kSquashBands> l {}, r {};
        split_[0].process(cLow_, cHigh_, left, l[0], l[1], l[2]);
        split_[1].process(cLow_, cHigh_, right, r[0], r[1], r[2]);
        // Make-up matches the level above the low band: below 200 Hz the growl
        // meets the high-passes after the growl clipper, so a lift there is not
        // heard as loudness, and must not be traded for the mids.
        const double inUpperL = l[1] + l[2], inUpperR = r[1] + r[2];
        inPower_ += makeupCoef_ * (0.5 * (inUpperL * inUpperL + inUpperR * inUpperR) - inPower_);
        double outL = 0.0, outR = 0.0, upperL = 0.0, upperR = 0.0;
        for (std::size_t b = 0; b < kSquashBands; ++b) {
            const double ms = 0.5 * (l[b] * l[b] + r[b] * r[b]);
            meanSquare_[b] += rmsCoef_[b] * (ms - meanSquare_[b]);
            double target = 0.0;
            if (amount > 0.0) {
                const double levelDb = 10.0 * std::log10(meanSquare_[b] + 1e-20);
                target = SquashCurve::gainDb(levelDb, amount);
            }
            const double coef = target < gainDb_[b] ? attackCoef_ : releaseCoef_;
            gainDb_[b] += coef * (target - gainDb_[b]);
            const double gain = gainDb_[b] == 0.0 ? 1.0 : dbToGain(gainDb_[b]);
            outL += gain * l[b];
            outR += gain * r[b];
            if (b > 0) {
                upperL += gain * l[b];
                upperR += gain * r[b];
            }
        }
        outPower_ += makeupCoef_ * (0.5 * (upperL * upperL + upperR * upperR) - outPower_);
        if (amount > 0.0) {
            const double match = 10.0 * std::log10((inPower_ + 1e-20) / (outPower_ + 1e-20));
            makeupDb_ = std::clamp(match, 0.0, kSquashMaxMakeupDb) + kSquashLiftDb * amount;
            const double gain = dbToGain(makeupDb_);
            outL *= gain;
            outR *= gain;
        } else {
            makeupDb_ = 0.0;
        }
        left = outL;
        right = outR;
    }

    double makeupDb() const noexcept TF_NONBLOCKING { return makeupDb_; }

    double bandGainDb(int band) const noexcept TF_NONBLOCKING { return gainDb_[static_cast<std::size_t>(band)]; }

private:
    SvfCoefs cLow_, cHigh_;
    std::array<ThreeBandSplit, 2> split_ {};
    std::array<double, kSquashBands> rmsCoef_ {};
    std::array<double, kSquashBands> meanSquare_ {};
    std::array<double, kSquashBands> gainDb_ {};
    double attackCoef_ = 1.0, releaseCoef_ = 1.0, makeupCoef_ = 1.0;
    double inPower_ = 0.0, outPower_ = 0.0, makeupDb_ = 0.0;
};

}
