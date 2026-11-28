// SPDX-License-Identifier: GPL-3.0-or-later
// Nonlinear zero-delay-feedback synth filters built from TPT one-poles
// (V. Zavalishin, "The Art of VA Filter Design", rev. 2.1, chs. 3-5).
//
// Each filter solves its feedback loop linearly for the current sample, then
// saturates the loop input and runs the stages with it. Saturating inside the
// loop keeps self-oscillation bounded at any resonance, and the one linear
// solve per sample keeps the cost fixed, which [[clang::nonblocking]] needs.
//
// `g` is the pre-warped cutoff, tan(pi fc / fs) (SvfCoefs::prewarp).
#pragma once

#include <hba/dsp/nonblocking.h>

#include <array>
#include <cmath>

namespace hba::dsp {

// Soft saturation with unit slope at zero that levels off at +-limit.
inline double softSaturate(double x, double limit) noexcept HBA_NONBLOCKING { return limit * std::tanh(x / limit); }

// Four TPT one-poles under global negative feedback (the Moog ladder's
// topology). `k` is the feedback, 0..4; the linear loop self-oscillates at the
// cutoff from k = 4. `drive` scales the input into the saturated loop.
class TptLadder {
  public:
    static constexpr double kSelfOscillation = 4.0;

    void reset() noexcept HBA_NONBLOCKING { s_.fill(0.0); }

    double process(double x, double g, double k, double drive) noexcept HBA_NONBLOCKING {
        const double G = g / (1.0 + g);
        const double a = 1.0 - G;
        // Each stage is y = G in + a s; the cascade's output is G^4 u + sigma.
        const double G2 = G * G;
        const double G4 = G2 * G2;
        const double sigma = a * (G2 * G * s_[0] + G2 * s_[1] + G * s_[2] + s_[3]);
        const double in = drive * x;
        const double estimate = (G4 * in + sigma) / (1.0 + k * G4);
        double y = softSaturate(in - k * estimate, kLimit);
        for (double& s : s_) {
            const double v = (y - s) * G;
            y = v + s;
            s = y + v;
        }
        return y;
    }

  private:
    static constexpr double kLimit = 1.5;
    std::array<double, 4> s_{};
};

// The Korg 35 low-pass of the MS-20: two TPT one-poles with the output fed
// back, through a one-pole high-pass, positively. `k` is the feedback, 0..2;
// the linear loop self-oscillates at the cutoff from k = 2. Unity gain at DC.
class TptKorg35 {
  public:
    static constexpr double kSelfOscillation = 2.0;

    void reset() noexcept HBA_NONBLOCKING { s1_ = s2_ = sh_ = 0.0; }

    double process(double x, double g, double k, double drive) noexcept HBA_NONBLOCKING {
        const double G = g / (1.0 + g);
        const double a = 1.0 - G;
        const double in = drive * x;
        // First low-pass.
        const double v1 = (in - s1_) * G;
        const double y1 = v1 + s1_;
        s1_ = y1 + v1;
        // y = G u + a s2 with u = y1 + k hp(y), hp(y) = a (y - sh): solve for y.
        const double estimate = (G * y1 - G * k * a * sh_ + a * s2_) / (1.0 - G * k * a);
        const double u = softSaturate(y1 + k * a * (estimate - sh_), kLimit);
        const double v2 = (u - s2_) * G;
        const double y = v2 + s2_;
        s2_ = y + v2;
        // The feedback high-pass's own low-pass state.
        const double vh = (y - sh_) * G;
        sh_ = vh + sh_ + vh;
        return y;
    }

  private:
    static constexpr double kLimit = 1.5;
    double s1_ = 0.0, s2_ = 0.0, sh_ = 0.0;
};

} // namespace hba::dsp
