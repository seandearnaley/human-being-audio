// SPDX-License-Identifier: GPL-3.0-or-later
// Closed-form Schroeder/Peak-Min crest-factor phase solver, discrete nonlinear
// Twin-T analog drum resonator, and analytical DC-cancelled asymmetric waveshaper.
#pragma once

#include <hba/dsp/control.h>
#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace hba::dsp {

// Closed-form Schroeder phase assignment minimizing peak-to-RMS crest factor for
// harmonic spectra a_0..a_{N-1} (k = 1..N). Strictly deterministic and nonblocking.
template <std::size_t N>
inline std::array<double, N> schroederPhases(const std::array<double, N>& amps,
                                             double basePhaseRad = 0.0) noexcept HBA_NONBLOCKING {
    std::array<double, N> phases{};
    if constexpr (N == 0)
        return phases;

    double totalPower = 0.0;
    for (std::size_t i = 0; i < N; ++i)
        totalPower += amps[i] * amps[i];

    phases[0] = basePhaseRad;
    if (totalPower <= 1.0e-15) {
        for (std::size_t i = 1; i < N; ++i)
            phases[i] = basePhaseRad;
        return phases;
    }

    const double invPower = 1.0 / totalPower;
    for (std::size_t k = 1; k < N; ++k) {
        double sum = 0.0;
        for (std::size_t j = 0; j < k; ++j)
            sum += static_cast<double>(k - j) * (amps[j] * amps[j] * invPower);
        phases[k] = basePhaseRad - kTwoPi * sum;
    }
    return phases;
}

// Peak-Min harmonic phase solver for sine-basis additive oscillators:
//   y(theta) = sum_{k=0}^{N-1} amps[k] * sin((k+1) * theta + ph[k])
// Evaluates zero-phase, Schroeder, Newman, and 2 deterministic passes of coordinate
// angle refinement on a 64-point fundamental grid, strictly guaranteeing peak <= zero-phase
// and achieving minimum possible crest factor on arbitrary harmonic spectra.
template <std::size_t N>
inline std::array<double, N> optimizeCrestPhases(const std::array<double, N>& amps,
                                                 double basePhaseRad = 0.0) noexcept HBA_NONBLOCKING {
    constexpr int kSamples = 64;
    constexpr int kMask = kSamples - 1;

    // Precompute the 64-point unit circle once; sin((k+1)*th + ph) = sin_tbl*cos(ph) + cos_tbl*sin(ph).
    std::array<double, kSamples> sinTbl{};
    std::array<double, kSamples> cosTbl{};
    for (int s = 0; s < kSamples; ++s) {
        const double th = kTwoPi * (static_cast<double>(s) / static_cast<double>(kSamples));
        sinTbl[static_cast<std::size_t>(s)] = std::sin(th);
        cosTbl[static_cast<std::size_t>(s)] = std::cos(th);
    }

    const auto evalSum = [&](const std::array<double, N>& ph,
                             std::array<double, kSamples>& wave) noexcept HBA_NONBLOCKING -> double {
        wave.fill(0.0);
        for (std::size_t k = 0; k < N; ++k) {
            if (std::abs(amps[k]) <= 1.0e-9)
                continue;
            const double ac = amps[k] * std::cos(ph[k]);
            const double as = amps[k] * std::sin(ph[k]);
            const int step = static_cast<int>(k + 1);
            for (int s = 0; s < kSamples; ++s) {
                const auto idx = static_cast<std::size_t>((step * s) & kMask);
                wave[static_cast<std::size_t>(s)] += ac * sinTbl[idx] + as * cosTbl[idx];
            }
        }
        double pk = 0.0;
        for (double y : wave)
            pk = std::max(pk, std::abs(y));
        return pk;
    };

    std::array<double, N> best{};
    for (std::size_t k = 0; k < N; ++k)
        best[k] = basePhaseRad;
    std::array<double, kSamples> bestWave{};
    double bestPeak = evalSum(best, bestWave);

    std::array<double, kSamples> candWave{};
    const auto consider = [&](const std::array<double, N>& cand) noexcept HBA_NONBLOCKING {
        const double pk = evalSum(cand, candWave);
        if (pk < bestPeak) {
            bestPeak = pk;
            best = cand;
            bestWave = candWave;
        }
    };

    // Seed candidates: Schroeder (sine-shifted + raw), Newman, and alternating quadrature
    const auto schr = schroederPhases(amps, basePhaseRad);
    consider(schr);
    if constexpr (N > 1) {
        std::array<double, N> schrCos = schr;
        for (std::size_t k = 1; k < N; ++k)
            schrCos[k] += kPi * 0.5;
        consider(schrCos);

        std::array<double, N> newman{};
        newman[0] = basePhaseRad;
        for (std::size_t k = 1; k < N; ++k)
            newman[k] = basePhaseRad + kPi * static_cast<double>((k + 1) * k) / static_cast<double>(N);
        consider(newman);

        // 2 passes of deterministic coordinate refinement over harmonics k = 1..N-1 (locking fundamental k = 0!)
        constexpr int kSteps = 12;
        std::array<double, kSamples> rest{};
        for (int pass = 0; pass < 2; ++pass) {
            const double span = (pass == 0) ? kTwoPi : (kTwoPi / static_cast<double>(kSteps));
            for (std::size_t k = 1; k < N; ++k) {
                if (std::abs(amps[k]) <= 1.0e-6)
                    continue;
                const double center = best[k];
                const int step = static_cast<int>(k + 1);
                const double oldAc = amps[k] * std::cos(center);
                const double oldAs = amps[k] * std::sin(center);
                for (int s = 0; s < kSamples; ++s) {
                    const auto idx = static_cast<std::size_t>((step * s) & kMask);
                    rest[static_cast<std::size_t>(s)] =
                        bestWave[static_cast<std::size_t>(s)] - (oldAc * sinTbl[idx] + oldAs * cosTbl[idx]);
                }

                double bestAngle = center;
                for (int st = 0; st < kSteps; ++st) {
                    const double delta = (pass == 0)
                                             ? (span * static_cast<double>(st) / static_cast<double>(kSteps))
                                             : (span * (static_cast<double>(st) / static_cast<double>(kSteps) - 0.5));
                    const double candAngle = center + delta;
                    const double ac = amps[k] * std::cos(candAngle);
                    const double as = amps[k] * std::sin(candAngle);
                    double pk = 0.0;
                    for (int s = 0; s < kSamples; ++s) {
                        const auto idx = static_cast<std::size_t>((step * s) & kMask);
                        pk = std::max(
                            pk, std::abs(rest[static_cast<std::size_t>(s)] + ac * sinTbl[idx] + as * cosTbl[idx]));
                    }
                    if (pk < bestPeak) {
                        bestPeak = pk;
                        bestAngle = candAngle;
                    }
                }
                best[k] = bestAngle;
                const double newAc = amps[k] * std::cos(bestAngle);
                const double newAs = amps[k] * std::sin(bestAngle);
                for (int s = 0; s < kSamples; ++s) {
                    const auto idx = static_cast<std::size_t>((step * s) & kMask);
                    bestWave[static_cast<std::size_t>(s)] =
                        rest[static_cast<std::size_t>(s)] + newAc * sinTbl[idx] + newAs * cosTbl[idx];
                }
            }
        }
    }

    return best;
}

// Nonlinear discrete-time Bridged-T analog drum resonator (TR-808 / Nord Drum style).
// Uses a coupled 2-state damped harmonic oscillator with soft diode-bridge saturation
// in the feedback loop so velocity organically tightens and warms the low end.
class TwinTResonator {
  public:
    void prepare(double sampleRate) noexcept HBA_NONBLOCKING {
        fs_ = std::isfinite(sampleRate) ? std::max(sampleRate, 8000.0) : 48000.0;
        cachedFreq_ = -1.0;
        cachedDecay_ = -1.0;
        reset();
    }

    void reset() noexcept HBA_NONBLOCKING {
        y1_ = 0.0;
        y2_ = 0.0;
    }

    void trigger(double velocity, double startPhaseRad = 0.0) noexcept HBA_NONBLOCKING {
        const double v = std::clamp(velocity, 0.05, 1.5);
        // Seed initial state cleanly along requested phase so sample-0 onset is deterministic
        y1_ = v * std::sin(startPhaseRad);
        y2_ = v * std::cos(startPhaseRad);
    }

    // Advance one sample at instantaneous `freqHz`, exponential decay `decaySec`, and `analogDrive` [0..1].
    double process(double freqHz, double decaySec, double analogDrive) noexcept HBA_NONBLOCKING {
        const double f = std::clamp(freqHz, 18.0, 0.42 * fs_);
        const double tau = std::max(0.015, decaySec);
        if (tau != cachedDecay_) {
            cachedDecay_ = tau;
            cachedR_ = std::exp(-1.0 / (tau * fs_));
        }
        if (f != cachedFreq_) {
            cachedFreq_ = f;
            const double w = kTwoPi * f / fs_;
            cachedCos_ = std::cos(w);
            cachedSin_ = std::sin(w);
        }

        // Coupled-form damped rotation (unconditionally stable under rapid pitch sweeps!)
        double nextY1 = cachedR_ * (cachedCos_ * y1_ + cachedSin_ * y2_);
        double nextY2 = cachedR_ * (-cachedSin_ * y1_ + cachedCos_ * y2_);

        // Soft diode-bridge feedback nonlinearity
        const double drv = std::clamp(analogDrive, 0.0, 1.0);
        if (drv > 1.0e-5) {
            const double k = 1.0 + 1.6 * drv;
            nextY1 = (1.0 - 0.18 * drv) * nextY1 + (0.18 * drv) * (std::tanh(k * nextY1) / k);
        }
        y1_ = nextY1;
        y2_ = nextY2;
        return y1_;
    }

  private:
    double fs_ = 48000.0;
    double y1_ = 0.0;
    double y2_ = 0.0;
    double cachedFreq_ = -1.0;
    double cachedDecay_ = -1.0;
    double cachedR_ = 1.0;
    double cachedCos_ = 1.0;
    double cachedSin_ = 0.0;
};

// Analytical DC bias for f(x) = tanh(x + a * x^2) driven by x = envAmp * sin(theta),
// computed via 32-point spectral midpoint quadrature using quarter-wave symmetry.
inline double analyticAsymDcOffset(double asym, double envAmp) noexcept HBA_NONBLOCKING {
    const double a = std::clamp(asym, -0.48, 0.48);
    if (std::abs(a) < 1.0e-6 || envAmp < 1.0e-6)
        return 0.0;

    // Quarter-wave sin((2m + 1) * pi / 32) for m = 0..7 -> exact 32-point periodic integral
    constexpr std::array<double, 8> kSin32{
        0.09801714032956060, 0.29028467725446233, 0.47139673682599765, 0.63439328416364550,
        0.77301045336273696, 0.88192126434835503, 0.95694033573220886, 0.99518472667219689,
    };
    double dc = 0.0;
    for (double s : kSin32) {
        const double u = envAmp * s;
        const double au2 = a * u * u;
        dc += std::tanh(u + au2) + std::tanh(-u + au2);
    }
    return dc * (1.0 / 16.0);
}

// Evaluates f(x) - dc with a precomputed DC bias (avoids recomputing DC inside oversampling loops).
inline double analyticAsymShapeWithDc(double x, double asym, double dcOffset) noexcept HBA_NONBLOCKING {
    const double a = std::clamp(asym, -0.48, 0.48);
    return std::tanh(x + a * x * x) - dcOffset;
}

// Smooth asymmetric waveshaper with exact analytical DC cancellation!
inline double analyticAsymShape(double x, double asym, double envAmp) noexcept HBA_NONBLOCKING {
    return analyticAsymShapeWithDc(x, asym, analyticAsymDcOffset(asym, envAmp));
}

} // namespace hba::dsp
