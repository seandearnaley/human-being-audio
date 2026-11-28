// SPDX-License-Identifier: GPL-3.0-or-later
// Exact unity-sum stereo transient / body separator (transient + body == input).
// Uses symmetric dual-time-constant RMS-power followers on a 2nd-order high-passed
// sidechain so steady periodic tones of any frequency converge to zero transient
// mask while sharp drum/pluck onsets pass intact.
#pragma once

#include <hba/dsp/control.h>
#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <cmath>

namespace hba::dsp {

class TransientSplit {
  public:
    void prepare(double sampleRate) noexcept HBA_NONBLOCKING {
        const double sr = std::isfinite(sampleRate) ? std::max(sampleRate, 1.0) : 48000.0;
        const double dt = 1.0 / sr;
        // Cascaded 1-pole high-pass at 240 Hz on detector mono sidechain to ignore sub cycles.
        hpCoef_ = std::exp(-kTwoPi * 240.0 * dt);
        // Symmetric one-pole lowpass filters on instantaneous power (x^2):
        // DC gain is identical (1.0) so any sustained harmonic tone yields fastPow == slowPow.
        fastPowCoef_ = onePoleCoef(0.0024, dt);
        slowPowCoef_ = onePoleCoef(0.038, dt);
        maskAtk_ = onePoleCoef(0.0004, dt);
        maskRel_ = onePoleCoef(0.022, dt);
        reset();
    }

    void reset() noexcept HBA_NONBLOCKING {
        hp1In_ = hp1Out_ = 0.0;
        hp2In_ = hp2Out_ = 0.0;
        fastPow_ = 0.0;
        slowPow_ = 0.0;
        mask_ = 0.0;
    }

    // Splits (inL, inR) into exact unity-sum transient and body signals:
    //   transL + bodyL == inL, transR + bodyR == inR.
    // `amount` in [0, 1] scales maximum transient extraction;
    // `sensitivity` in [0, 1] lowers the fast/slow RMS crest threshold.
    // Returns the smoothed transient mask in [0, 1] for metering.
    double process(double inL, double inR, double amount, double sensitivity, double& transL, double& transR,
                   double& bodyL, double& bodyR) noexcept HBA_NONBLOCKING {
        const double mono = 0.5 * (inL + inR);
        const double hp1 = hpCoef_ * (hp1Out_ + mono - hp1In_);
        hp1In_ = mono;
        hp1Out_ = hp1;
        const double hp2 = hpCoef_ * (hp2Out_ + hp1 - hp2In_);
        hp2In_ = hp1;
        hp2Out_ = hp2;

        // Include stereo side energy so crisp stereo hats/claps trigger cleanly too.
        const double side = 0.5 * (inL - inR);
        const double pow = hp2 * hp2 + side * side;

        fastPow_ += fastPowCoef_ * (pow - fastPow_);
        slowPow_ += slowPowCoef_ * (pow - slowPow_);

        const double fastRms = std::sqrt(std::max(0.0, fastPow_));
        const double slowRms = std::sqrt(std::max(0.0, slowPow_));
        const double denom = fastRms + 1.0e-4;
        const double rawRatio = std::max(0.0, fastRms - slowRms) / denom;

        // Map sensitivity [0..1] to onset threshold [0.36..0.12] and slope.
        const double sens = std::clamp(sensitivity, 0.0, 1.0);
        const double thresh = 0.36 - 0.24 * sens;
        const double slope = 2.4 + 2.8 * sens;
        const double target = std::clamp((rawRatio - thresh) * slope, 0.0, 1.0);

        mask_ += (target > mask_ ? maskAtk_ : maskRel_) * (target - mask_);

        const double m = std::clamp(mask_ * std::clamp(amount, 0.0, 1.0), 0.0, 1.0);
        transL = inL * m;
        transR = inR * m;
        bodyL = inL - transL;
        bodyR = inR - transR;
        return m;
    }

    double mask() const noexcept HBA_NONBLOCKING { return mask_; }

  private:
    double hpCoef_ = 0.97;
    double fastPowCoef_ = 0.01;
    double slowPowCoef_ = 0.0005;
    double maskAtk_ = 0.05;
    double maskRel_ = 0.001;

    double hp1In_ = 0.0;
    double hp1Out_ = 0.0;
    double hp2In_ = 0.0;
    double hp2Out_ = 0.0;
    double fastPow_ = 0.0;
    double slowPow_ = 0.0;
    double mask_ = 0.0;
};

} // namespace hba::dsp
