// SPDX-License-Identifier: GPL-3.0-or-later
// Canonical first- and second-order antiderivative anti-aliased (ADAA1/ADAA2)
// waveshapers for Human Being Audio engines.
//
// References:
//   - Parker, Zavalishin, Le Bivic, "Reducing the aliasing of nonlinear
//     waveshaping using continuous-time convolution", DAFx-16 (ADAA1).
//   - Bilbao, Esqueda, Parker, Valimaki, "Antiderivative Antialiasing for
//     Memoryless Nonlinearities", IEEE SPL / DAFx-17 (ADAA2 + turning-point limit).
//   - Esqueda, Pontynen, Valimaki, Parker, "Virtual Analog Buchla 259
//     Wavefolder", DAFx-17 (5-cell parallel West-Coast wavefolder).
//   - Chowdhury, "Practical Considerations for Antiderivative Anti-Aliasing"
//     (cached antiderivative state, delay alignment, ill-conditioning guards).
#pragma once

#include <hba/dsp/nonblocking.h>

#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>

namespace hba::dsp {

#define HBA_DSP_NONBLOCKING HBA_NONBLOCKING

namespace detail {
inline constexpr double kPi = 3.1415926535897932384626433832795;
inline constexpr double kLn2 = 0.69314718055994530941723212145818;
inline constexpr double kPi2Over24 = (kPi * kPi) / 24.0;

// Clenshaw evaluation of Li_2(y) on y in [0, 0.5] accurate to ~1e-15 in double.
inline constexpr std::array<double, 18> kDilogCheb {
    +1.07636827994462037e+00, +8.16965805805102974e-02, +5.81415714077889324e-03,
    +5.37161981454234699e-04, +5.72470467518688177e-05, +6.67454612186042043e-06,
    +8.27646734270616267e-07, +1.07331567263576247e-07, +1.44007730061722511e-08,
    +1.98444146300621104e-09, +2.79399367839358924e-10, +4.00402673979745811e-11,
    +5.82399560676726164e-12, +8.57425241918008396e-13, +1.26904659620347766e-13,
    +1.87442653990880606e-14, +3.52804205603105309e-15, -2.99143426079556090e-16,
};

inline double dilogPosHalf(double y) noexcept HBA_DSP_NONBLOCKING {
    const double t = 4.0 * y - 1.0;
    const double twoT = 2.0 * t;
    double b1 = 0.0;
    double b2 = 0.0;
    for (std::size_t k = kDilogCheb.size() - 1; k >= 1; --k) {
        const double b0 = twoT * b1 - b2 + kDilogCheb[k];
        b2 = b1;
        b1 = b0;
    }
    return y * (t * b1 - b2 + kDilogCheb[0]);
}
}  // namespace detail

template <typename S>
concept Adaa1Shaper = requires(double u) {
    { S::eval(u) } noexcept -> std::same_as<double>;
    { S::ad1(u) } noexcept -> std::same_as<double>;
};

template <typename S>
concept Adaa2Shaper = Adaa1Shaper<S> && requires(double u) {
    { S::ad2(u) } noexcept -> std::same_as<double>;
};

// C^2-smooth headroom guard: exact identity for |y| <= knee, matching value,
// unity slope, and zero curvature at |y| == knee, asymptoting to +/-ceiling.
inline double softGuard(double y, double knee = 2.5, double ceiling = 4.0) noexcept HBA_DSP_NONBLOCKING {
    if (!std::isfinite(y))
        return 0.0;
    const double a = std::abs(y);
    if (a <= knee) return y;
    const double head = ceiling - knee;
    if (head <= 1.0e-12)
        return y < 0.0 ? -ceiling : ceiling;
    const double g = knee + head * std::tanh((a - knee) / head);
    return y < 0.0 ? -g : g;
}

// -----------------------------------------------------------------------------
// Canonical double-precision waveshaper traits (all odd, unity slope at 0,
// bounded output, exact analytical 1st and 2nd antiderivatives).
// -----------------------------------------------------------------------------

// Hyperbolic tangent soft saturator: f(u) = tanh(u).
// Maclaurin polynomials for |u| <= 0.5 eliminate catastrophic cancellation near 0;
// Landen-transformed dilogarithm for |u| > 0.5 gives ~1e-15 seam continuity.
struct TanhShaper {
    static constexpr std::array<double, 15> kC1 {
        +5.00000000000000000e-01, -8.33333333333333287e-02, +2.22222222222222231e-02,
        -6.74603174603174635e-03, +2.18694885361552047e-03, -7.38602960825183075e-04,
        +2.56580574040891495e-04, -9.09896491907073956e-05, +3.27793022747547793e-05,
        -1.19564557121776246e-05, +4.40524452587702324e-06, -1.63659682847153484e-06,
        +6.12265579589575608e-07, -2.30417471987693944e-07, +8.71590383763584821e-08,
    };
    static constexpr std::array<double, 15> kC2 {
        +1.66666666666666657e-01, -1.66666666666666664e-02, +3.17460317460317460e-03,
        -7.49559082892416249e-04, +1.98813532146865468e-04, -5.68156123711679247e-05,
        +1.71053716027260985e-05, -5.35233230533572931e-06, +1.72522643551340920e-06,
        -5.69355033913220147e-07, +1.91532370690305345e-07, -6.54638731388613892e-08,
        +2.26765029477620576e-08, -7.94543006854117066e-09, +2.81158188310833838e-09,
    };

    static double eval(double u) noexcept HBA_DSP_NONBLOCKING { return std::tanh(u); }

    static double ad1(double u) noexcept HBA_DSP_NONBLOCKING {
        const double a = std::abs(u);
        if (a <= 0.5) {
            const double u2 = u * u;
            double p = 0.0;
            for (std::size_t i = kC1.size(); i-- > 0;) p = p * u2 + kC1[i];
            return u2 * p;
        }
        return a + std::log1p(std::exp(-2.0 * a)) - detail::kLn2;
    }

    static double ad2(double u) noexcept HBA_DSP_NONBLOCKING {
        const double a = std::abs(u);
        if (a <= 0.5) {
            const double u2 = u * u;
            double p = 0.0;
            for (std::size_t i = kC2.size(); i-- > 0;) p = p * u2 + kC2[i];
            return u * u2 * p;
        }
        const double x = std::exp(-2.0 * a);
        const double lp = std::log1p(x);
        const double li2Neg = -detail::dilogPosHalf(x / (1.0 + x)) - 0.5 * lp * lp;
        const double val = 0.5 * a * a - detail::kLn2 * a + detail::kPi2Over24 + 0.5 * li2Neg;
        return u < 0.0 ? -val : val;
    }
};

// Hard clipper: f(u) = clamp(u, -1, 1).
struct HardClipShaper {
    static double eval(double u) noexcept HBA_DSP_NONBLOCKING {
        if (u > 1.0) return 1.0;
        if (u < -1.0) return -1.0;
        return u;
    }

    static double ad1(double u) noexcept HBA_DSP_NONBLOCKING {
        const double a = std::abs(u);
        return a <= 1.0 ? 0.5 * a * a : a - 0.5;
    }

    static double ad2(double u) noexcept HBA_DSP_NONBLOCKING {
        const double a = std::abs(u);
        const double val = a <= 1.0 ? (1.0 / 6.0) * a * a * a : 0.5 * a * a - 0.5 * a + (1.0 / 6.0);
        return u < 0.0 ? -val : val;
    }
};

// Symmetric triangle wavefolder, period 4, identity on [-1, 1].
// Both f and F1 have exact zero mean over a period, so F2 is strictly periodic.
struct TriangleFoldShaper {
    static double eval(double u) noexcept HBA_DSP_NONBLOCKING {
        const double v = u + 1.0 - 4.0 * std::floor((u + 1.0) * 0.25);
        return 1.0 - std::abs(v - 2.0);
    }

    static double ad1(double u) noexcept HBA_DSP_NONBLOCKING {
        const double v = u + 1.0 - 4.0 * std::floor((u + 1.0) * 0.25);
        return v <= 2.0 ? 0.5 * v * v - v : -0.5 * v * v + 3.0 * v - 4.0;
    }

    static double ad2(double u) noexcept HBA_DSP_NONBLOCKING {
        const double v = u + 1.0 - 4.0 * std::floor((u + 1.0) * 0.25);
        if (v <= 2.0) return ((1.0 / 6.0) * v - 0.5) * v * v + (1.0 / 3.0);
        return (((-1.0 / 6.0) * v + 1.5) * v - 4.0) * v + 3.0;
    }
};

// West-Coast 5-cell parallel Buchla 259 wavefolder (Esqueda et al., DAFx-17),
// normalized to unity slope on [-1, 1] with exact op-amp rail saturation beyond
// the 5th fold so output remains strictly bounded at extreme drive.
struct Buchla259Shaper {
    struct Cell {
        double thresh;
        double weight;
    };

    static constexpr std::array<Cell, 5> kCells {{
        { 1.00000000000000000e+00, -2.00000000000000000e+00 },
        { 3.00000000000000000e+00, +2.02693441999999990e+00 },
        { 4.99000000000000021e+00, -2.09327472000000014e+00 },
        { 6.80500000000000060e+00, +1.94396597999999976e+00 },
        { 9.09999999999999964e+00, -1.21239623999999990e+00 },
    }};
    static constexpr double kRailThresh = 1.54396936304673744e+01;
    static constexpr double kRailY = -1.0;
    static constexpr double kRailA1 = +1.33347853657052084e+00;
    static constexpr double kRailA2 = +1.91552893313865766e+01;

    static double eval(double u) noexcept HBA_DSP_NONBLOCKING {
        const double a = std::abs(u);
        double y = kRailY;
        if (a < kRailThresh) {
            y = a;
            for (const auto& c : kCells)
                if (a > c.thresh) y += c.weight * (a - c.thresh);
        }
        return u < 0.0 ? -y : y;
    }

    static double ad1(double u) noexcept HBA_DSP_NONBLOCKING {
        const double a = std::abs(u);
        if (a >= kRailThresh) return kRailA1 + kRailY * (a - kRailThresh);
        double y = 0.5 * a * a;
        for (const auto& c : kCells) {
            if (a > c.thresh) {
                const double d = a - c.thresh;
                y += 0.5 * c.weight * d * d;
            }
        }
        return y;
    }

    static double ad2(double u) noexcept HBA_DSP_NONBLOCKING {
        const double a = std::abs(u);
        double y = 0.0;
        if (a >= kRailThresh) {
            const double d = a - kRailThresh;
            y = kRailA2 + kRailA1 * d + 0.5 * kRailY * d * d;
        } else {
            y = (1.0 / 6.0) * a * a * a;
            for (const auto& c : kCells) {
                if (a > c.thresh) {
                    const double d = a - c.thresh;
                    y += (1.0 / 6.0) * c.weight * d * d * d;
                }
            }
        }
        return u < 0.0 ? -y : y;
    }
};

// Sinusoidal wavefolder: f(u) = sin(u) (exact unity small-signal slope at 0,
// matching Tanh, HardClip, TriangleFold, and Buchla259).
struct SineFoldShaper {
    static double eval(double u) noexcept HBA_DSP_NONBLOCKING { return std::sin(u); }
    static double ad1(double u) noexcept HBA_DSP_NONBLOCKING { return -std::cos(u); }
    static double ad2(double u) noexcept HBA_DSP_NONBLOCKING { return -std::sin(u); }
};

// -----------------------------------------------------------------------------
// First-order ADAA processor (0.5-sample group delay, cached F1 state).
// -----------------------------------------------------------------------------
template <Adaa1Shaper Shaper>
class Adaa1 {
public:
    static constexpr double kEps = 1.0e-5;

    Adaa1() noexcept HBA_DSP_NONBLOCKING { reset(); }

    void reset() noexcept HBA_DSP_NONBLOCKING { prime(0.0); }

    void prime(double u1) noexcept HBA_DSP_NONBLOCKING {
        x1_ = u1;
        f1_1_ = Shaper::ad1(u1);
    }

    double process(double x0) noexcept HBA_DSP_NONBLOCKING {
        const double f1_0 = Shaper::ad1(x0);
        const double diff = x0 - x1_;
        const double y = std::abs(diff) > kEps ? (f1_0 - f1_1_) / diff : Shaper::eval(0.5 * (x0 + x1_));
        x1_ = x0;
        f1_1_ = f1_0;
        return y;
    }

private:
    double x1_ = 0.0;
    double f1_1_ = 0.0;
};

// -----------------------------------------------------------------------------
// Second-order ADAA processor (exact 1.0-sample integer group delay, cached F2
// and D1 state, Bilbao et al. DAFx-17 analytical turning-point fallback).
// -----------------------------------------------------------------------------
template <Adaa2Shaper Shaper>
class Adaa2 {
public:
    static constexpr double kEps1 = 1.0e-5;
    static constexpr double kEps2 = 1.0e-4;
    static constexpr double kEpsTurn = 1.0e-4;

    Adaa2() noexcept HBA_DSP_NONBLOCKING { reset(); }

    void reset() noexcept HBA_DSP_NONBLOCKING { prime(0.0, 0.0); }

    void prime(double u1, double u2) noexcept HBA_DSP_NONBLOCKING {
        x1_ = u1;
        x2_ = u2;
        f2_1_ = Shaper::ad2(u1);
        const double diff = u1 - u2;
        d1_1_ = std::abs(diff) > kEps1 ? (f2_1_ - Shaper::ad2(u2)) / diff : Shaper::ad1(0.5 * (u1 + u2));
    }

    double process(double x0) noexcept HBA_DSP_NONBLOCKING {
        const double f2_0 = Shaper::ad2(x0);
        const double diff01 = x0 - x1_;
        const double d1_0 = std::abs(diff01) > kEps1 ? (f2_0 - f2_1_) / diff01 : Shaper::ad1(0.5 * (x0 + x1_));

        const double diff02 = x0 - x2_;
        double y = 0.0;
        if (std::abs(diff02) > kEps2) {
            y = 2.0 * (d1_0 - d1_1_) / diff02;
        } else {
            // Exact turning-point limit as x0, x2 -> xBar with x1 arbitrary.
            const double xBar = 0.5 * (x0 + x2_);
            const double delta = xBar - x1_;
            if (std::abs(delta) > kEpsTurn) {
                const double f2Bar = Shaper::ad2(xBar);
                const double d1Bar = (f2Bar - f2_1_) / delta;
                y = 2.0 * (Shaper::ad1(xBar) - d1Bar) / delta;
            } else {
                // 3rd-order accurate centroid limit when x0 ~= x1 ~= x2.
                y = Shaper::eval((x0 + x1_ + x2_) * (1.0 / 3.0));
            }
        }

        x2_ = x1_;
        x1_ = x0;
        f2_1_ = f2_0;
        d1_1_ = d1_0;
        return y;
    }

private:
    double x1_ = 0.0;
    double x2_ = 0.0;
    double f2_1_ = 0.0;
    double d1_1_ = 0.0;
};

// -----------------------------------------------------------------------------
// Studio 5-model ADAA2 switcher with click-free crossfade, exact 1-sample dry
// alignment, automatic A->B->A re-priming, and centroid DC-offset cancellation.
// -----------------------------------------------------------------------------
enum class ShaperModel : std::uint8_t { fold = 0, sine = 1, tube = 2, buchla = 3, clip = 4 };

inline double evalShaper(ShaperModel model, double u) noexcept HBA_DSP_NONBLOCKING {
    switch (model) {
    case ShaperModel::fold:
        return TriangleFoldShaper::eval(u);
    case ShaperModel::sine:
        return SineFoldShaper::eval(u);
    case ShaperModel::tube:
        return TanhShaper::eval(u);
    case ShaperModel::buchla:
        return Buchla259Shaper::eval(u);
    case ShaperModel::clip:
        return HardClipShaper::eval(u);
    }
    return u;
}

inline double ad1Shaper(ShaperModel model, double u) noexcept HBA_DSP_NONBLOCKING {
    switch (model) {
    case ShaperModel::fold:
        return TriangleFoldShaper::ad1(u);
    case ShaperModel::sine:
        return SineFoldShaper::ad1(u);
    case ShaperModel::tube:
        return TanhShaper::ad1(u);
    case ShaperModel::buchla:
        return Buchla259Shaper::ad1(u);
    case ShaperModel::clip:
        return HardClipShaper::ad1(u);
    }
    return 0.5 * u * u;
}

class MultiShaperAdaa2 {
  public:
    void reset() noexcept HBA_DSP_NONBLOCKING {
        fold_.reset();
        sine_.reset();
        tube_.reset();
        buchla_.reset();
        clip_.reset();
        x1_ = 0.0;
        u1_ = 0.0;
        u2_ = 0.0;
        offset1_ = 0.0;
        offset2_ = 0.0;
        activeTo_ = ShaperModel::fold;
        activeFrom_ = ShaperModel::fold;
        primed_ = false;
    }

    // Advances the 1-sample dry alignment register while bypassing shaping.
    double bypass(double x) noexcept HBA_DSP_NONBLOCKING {
        const double dry = x1_;
        x1_ = x;
        primed_ = false;
        return dry;
    }

    // Processes `u = gain * x + offset` with crossfade `mix` (0 = `from`, 1 = `to`),
    // returning both the centroid-bias-cancelled shaped output and exact 1-sample dry.
    double process(double x, double gain, double offset, ShaperModel from, ShaperModel to, double mix,
                   double& outDry) noexcept HBA_DSP_NONBLOCKING {
        const double dry = x1_;
        x1_ = x;
        outDry = dry;
        const double u = gain * x + offset;

        if (!primed_) {
            activeTo_ = to;
            activeFrom_ = from;
            u1_ = gain * dry + offset;
            u2_ = u1_;
            offset1_ = offset;
            offset2_ = offset;
            primeModel(to, u1_, u2_);
            if (mix < 1.0 && from != to)
                primeModel(from, u1_, u2_);
            primed_ = true;
        } else {
            const ShaperModel prevTo = activeTo_;
            const ShaperModel prevFrom = activeFrom_;
            if (to != prevTo) {
                if (to != prevFrom)
                    primeModel(to, u1_, u2_);
                activeTo_ = to;
            }
            if (mix < 1.0 && from != prevFrom) {
                if (from != prevTo && from != to)
                    primeModel(from, u1_, u2_);
                activeFrom_ = from;
            }
        }

        const double offsetCentroid = (offset + offset1_ + offset2_) * (1.0 / 3.0);
        double wet = tickModel(to, u) - evalShaper(to, offsetCentroid);
        if (mix < 1.0 && from != to) {
            const double wetFrom = tickModel(from, u) - evalShaper(from, offsetCentroid);
            wet = mix * wet + (1.0 - mix) * wetFrom;
        } else {
            activeFrom_ = activeTo_;
        }

        u2_ = u1_;
        u1_ = u;
        offset2_ = offset1_;
        offset1_ = offset;
        return wet;
    }

  private:
    void primeModel(ShaperModel model, double u1, double u2) noexcept HBA_DSP_NONBLOCKING {
        switch (model) {
        case ShaperModel::fold:
            fold_.prime(u1, u2);
            return;
        case ShaperModel::sine:
            sine_.prime(u1, u2);
            return;
        case ShaperModel::tube:
            tube_.prime(u1, u2);
            return;
        case ShaperModel::buchla:
            buchla_.prime(u1, u2);
            return;
        case ShaperModel::clip:
            clip_.prime(u1, u2);
            return;
        }
    }

    double tickModel(ShaperModel model, double u) noexcept HBA_DSP_NONBLOCKING {
        switch (model) {
        case ShaperModel::fold:
            return fold_.process(u);
        case ShaperModel::sine:
            return sine_.process(u);
        case ShaperModel::tube:
            return tube_.process(u);
        case ShaperModel::buchla:
            return buchla_.process(u);
        case ShaperModel::clip:
            return clip_.process(u);
        }
        return u;
    }

    Adaa2<TriangleFoldShaper> fold_{};
    Adaa2<SineFoldShaper> sine_{};
    Adaa2<TanhShaper> tube_{};
    Adaa2<Buchla259Shaper> buchla_{};
    Adaa2<HardClipShaper> clip_{};

    double x1_ = 0.0;
    double u1_ = 0.0;
    double u2_ = 0.0;
    double offset1_ = 0.0;
    double offset2_ = 0.0;
    ShaperModel activeTo_ = ShaperModel::fold;
    ShaperModel activeFrom_ = ShaperModel::fold;
    bool primed_ = false;
};

#undef HBA_DSP_NONBLOCKING

}  // namespace hba::dsp
