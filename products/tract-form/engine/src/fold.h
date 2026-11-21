// The fold: x' = g x + 0.5 asym through a fixed shaper, with first-order
// antiderivative anti-aliasing (Parker, Zavalishin, Le Bivic, "Reducing the
// aliasing of nonlinear waveshaping using continuous-time convolution",
// DAFx-16): y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]), falling back to
// f((x[n] + x[n-1]) / 2) when the difference is ill-conditioned. The wet signal
// is DC-blocked at 10 Hz and mixed with the dry input by `warp`.
#pragma once

#include "control.h"
#include "feedback_delay.h"
#include "tf/params.h"

#include <cmath>

namespace tf::dsp {

// Shapers, all odd, unity slope at 0 and bounded by +-1.
//   fold: triangle fold, period 4 (identity on [-1, 1])
//   sine: sin(pi/2 u)
//   tube: tanh(u)
inline double shape(FoldType type, double u) noexcept TF_NONBLOCKING {
    switch (type) {
        case FoldType::fold: {
            const double v = u + 1.0 - 4.0 * std::floor((u + 1.0) * 0.25);  // [0, 4)
            return 1.0 - std::abs(v - 2.0);
        }
        case FoldType::sine: return std::sin(0.5 * kPi * u);
        case FoldType::tube: return std::tanh(u);
    }
    return u;
}

// Antiderivatives of the shapers (any constant offset cancels in ADAA).
inline double shapeAntiderivative(FoldType type, double u) noexcept TF_NONBLOCKING {
    switch (type) {
        case FoldType::fold: {
            const double v = u + 1.0 - 4.0 * std::floor((u + 1.0) * 0.25);
            return v <= 2.0 ? 0.5 * v * v - v : -0.5 * v * v + 3.0 * v - 4.0;
        }
        case FoldType::sine: return -(2.0 / kPi) * std::cos(0.5 * kPi * u);
        case FoldType::tube: {
            const double a = std::abs(u);  // log cosh u, overflow-free
            return a + std::log1p(std::exp(-2.0 * a)) - 0.6931471805599453;
        }
    }
    return 0.5 * u * u;
}

// Below this input difference the ADAA quotient is replaced by the midpoint value.
inline constexpr double kAdaaEpsilon = 1e-5;

inline double adaa(FoldType type, double u, double u1) noexcept TF_NONBLOCKING {
    const double d = u - u1;
    if (std::abs(d) < kAdaaEpsilon) return shape(type, 0.5 * (u + u1));
    return (shapeAntiderivative(type, u) - shapeAntiderivative(type, u1)) / d;
}

class Folder {
public:
    void prepare(double sampleRate) noexcept TF_NONBLOCKING { dc_.prepare(10.0, sampleRate); }
    void reset() noexcept TF_NONBLOCKING {
        dc_.reset();
        u1_ = 0.0;
    }

    // `mix` crossfades from shaper `from` (0) to `to` (1) when the type changes.
    double process(double x, double gain, double offset, double warp, FoldType from, FoldType to,
                   double mix) noexcept TF_NONBLOCKING {
        const double u = gain * x + offset;
        double wet = adaa(to, u, u1_);
        if (mix < 1.0) wet = mix * wet + (1.0 - mix) * adaa(from, u, u1_);
        u1_ = u;
        wet = dc_.process(wet);
        return (1.0 - warp) * x + warp * wet;
    }

private:
    DcBlocker dc_;
    double u1_ = 0.0;
};

}
