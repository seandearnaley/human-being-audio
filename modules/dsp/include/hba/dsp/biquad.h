// SPDX-License-Identifier: GPL-3.0-or-later
// Standard RBJ Audio EQ Cookbook biquad coefficients and DF-II Transposed state.
#pragma once

#include <hba/dsp/control.h>
#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <cmath>

namespace hba::dsp {

struct BiquadCoefs {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;

    static BiquadCoefs bypass() noexcept HBA_NONBLOCKING { return {}; }

    static BiquadCoefs lowpass(double hz, double q, double sampleRate) noexcept HBA_NONBLOCKING {
        if (!(sampleRate > 0.0) || !(q > 0.0) || !std::isfinite(hz) || !std::isfinite(q) || !std::isfinite(sampleRate))
            return bypass();
        const double maxHz = 0.48 * sampleRate;
        const double f = std::clamp(hz, std::min(10.0, maxHz), maxHz);
        const double w0 = kTwoPi * f / sampleRate;
        const double c = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * std::max(0.05, q));
        const double invA0 = 1.0 / (1.0 + alpha);
        return {0.5 * (1.0 - c) * invA0, (1.0 - c) * invA0, 0.5 * (1.0 - c) * invA0, -2.0 * c * invA0,
                (1.0 - alpha) * invA0};
    }

    static BiquadCoefs highpass(double hz, double q, double sampleRate) noexcept HBA_NONBLOCKING {
        if (!(sampleRate > 0.0) || !(q > 0.0) || !std::isfinite(hz) || !std::isfinite(q) || !std::isfinite(sampleRate))
            return bypass();
        const double maxHz = 0.48 * sampleRate;
        const double f = std::clamp(hz, std::min(10.0, maxHz), maxHz);
        const double w0 = kTwoPi * f / sampleRate;
        const double c = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * std::max(0.05, q));
        const double invA0 = 1.0 / (1.0 + alpha);
        return {0.5 * (1.0 + c) * invA0, -(1.0 + c) * invA0, 0.5 * (1.0 + c) * invA0, -2.0 * c * invA0,
                (1.0 - alpha) * invA0};
    }

    static BiquadCoefs bandpass(double hz, double q, double sampleRate) noexcept HBA_NONBLOCKING {
        if (!(sampleRate > 0.0) || !(q > 0.0) || !std::isfinite(hz) || !std::isfinite(q) || !std::isfinite(sampleRate))
            return bypass();
        const double maxHz = 0.48 * sampleRate;
        const double f = std::clamp(hz, std::min(10.0, maxHz), maxHz);
        const double w0 = kTwoPi * f / sampleRate;
        const double c = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * std::max(0.05, q));
        const double invA0 = 1.0 / (1.0 + alpha);
        return {alpha * invA0, 0.0, -alpha * invA0, -2.0 * c * invA0, (1.0 - alpha) * invA0};
    }

    static BiquadCoefs peak(double hz, double q, double gainDb, double sampleRate) noexcept HBA_NONBLOCKING {
        if (!(sampleRate > 0.0) || !(q > 0.0) || !std::isfinite(hz) || !std::isfinite(q) || !std::isfinite(gainDb) ||
            !std::isfinite(sampleRate))
            return bypass();
        const double maxHz = 0.48 * sampleRate;
        const double f = std::clamp(hz, std::min(10.0, maxHz), maxHz);
        const double a = std::pow(10.0, std::clamp(gainDb, -96.0, 96.0) / 40.0);
        const double w0 = kTwoPi * f / sampleRate;
        const double c = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * std::max(0.05, q));
        const double invA0 = 1.0 / (1.0 + alpha / a);
        return {(1.0 + alpha * a) * invA0, -2.0 * c * invA0, (1.0 - alpha * a) * invA0, -2.0 * c * invA0,
                (1.0 - alpha / a) * invA0};
    }

    static BiquadCoefs lowShelf(double hz, double slope, double gainDb, double sampleRate) noexcept HBA_NONBLOCKING {
        if (!(sampleRate > 0.0) || !(slope > 0.0) || !std::isfinite(slope) || !std::isfinite(hz) ||
            !std::isfinite(gainDb) || !std::isfinite(sampleRate))
            return bypass();
        const double maxHz = 0.48 * sampleRate;
        const double f = std::clamp(hz, std::min(10.0, maxHz), maxHz);
        const double a = std::pow(10.0, std::clamp(gainDb, -96.0, 96.0) / 40.0);
        const double w0 = kTwoPi * f / sampleRate;
        const double c = std::cos(w0);
        const double s = std::sin(w0);
        const double alpha =
            0.5 * s * std::sqrt(std::max(1.0e-6, (a + 1.0 / a) * (1.0 / std::clamp(slope, 0.1, 2.0) - 1.0) + 2.0));
        const double twoSqrtAAlpha = 2.0 * std::sqrt(a) * alpha;
        const double invA0 = 1.0 / ((a + 1.0) + (a - 1.0) * c + twoSqrtAAlpha);
        return {a * ((a + 1.0) - (a - 1.0) * c + twoSqrtAAlpha) * invA0, 2.0 * a * ((a - 1.0) - (a + 1.0) * c) * invA0,
                a * ((a + 1.0) - (a - 1.0) * c - twoSqrtAAlpha) * invA0, -2.0 * ((a - 1.0) + (a + 1.0) * c) * invA0,
                ((a + 1.0) + (a - 1.0) * c - twoSqrtAAlpha) * invA0};
    }

    static BiquadCoefs highShelf(double hz, double slope, double gainDb, double sampleRate) noexcept HBA_NONBLOCKING {
        if (!(sampleRate > 0.0) || !(slope > 0.0) || !std::isfinite(slope) || !std::isfinite(hz) ||
            !std::isfinite(gainDb) || !std::isfinite(sampleRate))
            return bypass();
        const double maxHz = 0.48 * sampleRate;
        const double f = std::clamp(hz, std::min(10.0, maxHz), maxHz);
        const double a = std::pow(10.0, std::clamp(gainDb, -96.0, 96.0) / 40.0);
        const double w0 = kTwoPi * f / sampleRate;
        const double c = std::cos(w0);
        const double s = std::sin(w0);
        const double alpha =
            0.5 * s * std::sqrt(std::max(1.0e-6, (a + 1.0 / a) * (1.0 / std::clamp(slope, 0.1, 2.0) - 1.0) + 2.0));
        const double twoSqrtAAlpha = 2.0 * std::sqrt(a) * alpha;
        const double invA0 = 1.0 / ((a + 1.0) - (a - 1.0) * c + twoSqrtAAlpha);
        return {a * ((a + 1.0) + (a - 1.0) * c + twoSqrtAAlpha) * invA0, -2.0 * a * ((a - 1.0) + (a + 1.0) * c) * invA0,
                a * ((a + 1.0) + (a - 1.0) * c - twoSqrtAAlpha) * invA0, 2.0 * ((a - 1.0) - (a + 1.0) * c) * invA0,
                ((a + 1.0) - (a - 1.0) * c - twoSqrtAAlpha) * invA0};
    }
};

struct BiquadState {
    double z1 = 0.0;
    double z2 = 0.0;

    void reset() noexcept HBA_NONBLOCKING { z1 = z2 = 0.0; }

    double process(const BiquadCoefs& c, double x) noexcept HBA_NONBLOCKING {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

} // namespace hba::dsp
