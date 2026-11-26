// SPDX-License-Identifier: GPL-3.0-or-later
#include "pf/cubes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <utility>

namespace pf {

namespace {
constexpr double kPi = 3.14159265358979323846;

double logTrilerp(double c000, double c100, double c010, double c110, double c001, double c101, double c011,
                  double c111, double x, double y, double z) noexcept PF_NONBLOCKING {
    const double l00 = std::log(c000) * (1.0 - x) + std::log(c100) * x;
    const double l10 = std::log(c010) * (1.0 - x) + std::log(c110) * x;
    const double l01 = std::log(c001) * (1.0 - x) + std::log(c101) * x;
    const double l11 = std::log(c011) * (1.0 - x) + std::log(c111) * x;
    const double f0 = l00 * (1.0 - y) + l10 * y;
    const double f1 = l01 * (1.0 - y) + l11 * y;
    return std::exp(f0 * (1.0 - z) + f1 * z);
}

double linTrilerp(double c000, double c100, double c010, double c110, double c001, double c101, double c011,
                  double c111, double x, double y, double z) noexcept PF_NONBLOCKING {
    const double l00 = c000 * (1.0 - x) + c100 * x;
    const double l10 = c010 * (1.0 - x) + c110 * x;
    const double l01 = c001 * (1.0 - x) + c101 * x;
    const double l11 = c011 * (1.0 - x) + c111 * x;
    const double f0 = l00 * (1.0 - y) + l10 * y;
    const double f1 = l01 * (1.0 - y) + l11 * y;
    return f0 * (1.0 - z) + f1 * z;
}

// Fast zero-trig continuous-time L4 passband gain estimator using moderated Q
// (<= 2.2) so passband auto-makeup stays 100% smooth as high-Q poles sweep
// across frequency, while mathematically preventing cumulative dipole shelf blowups.
double estimatePassbandGain(const std::array<SectionSpec, kNumStages>& stages, int activeStages,
                            int topology) noexcept PF_NONBLOCKING {
    constexpr std::array<double, 13> kProbes{75.0,   115.0,  175.0,  270.0,  415.0,  640.0,  980.0,
                                             1500.0, 2300.0, 3550.0, 5450.0, 8400.0, 12500.0};
    const int n = std::clamp(activeStages, 1, kNumStages);
    const double invSqrtN = 1.0 / std::sqrt(static_cast<double>(n));

    auto stageEval = [](double hz, const SectionSpec& s) noexcept PF_NONBLOCKING {
        const double fp = static_cast<double>(s.poleHz);
        const double r = static_cast<double>(s.zeroHz) / fp;
        const double norm = 1.0 / std::max(0.25, r);
        const double kp = 1.0 / std::min(static_cast<double>(s.poleQ), 2.2);
        const double kz = 1.0 / std::min(static_cast<double>(s.zeroQ), 2.2);
        const double w = hz / fp;
        const double w2 = w * w;
        const double nRe = norm * (r * r - w2);
        const double nIm = norm * (r * kz * w);
        const double dRe = 1.0 - w2;
        const double dIm = kp * w;
        const double dMag2 = std::max(dRe * dRe + dIm * dIm, 1.0e-18);
        return std::pair{(nRe * dRe + nIm * dIm) / dMag2, (nIm * dRe - nRe * dIm) / dMag2};
    };

    auto stageBpEval = [](double hz, const SectionSpec& s) noexcept PF_NONBLOCKING {
        const double fp = static_cast<double>(s.poleHz);
        const double kp = 1.0 / std::min(static_cast<double>(s.poleQ), 2.2);
        const double g = std::sqrt(kp);
        const double w = hz / fp;
        const double nIm = g * w;
        const double dRe = 1.0 - w * w;
        const double dIm = kp * w;
        const double dMag2 = std::max(dRe * dRe + dIm * dIm, 1.0e-18);
        return std::pair{(nIm * dIm) / dMag2, (nIm * dRe) / dMag2};
    };

    double sumMag4 = 0.0;
    for (double hz : kProbes) {
        double tRe = 1.0, tIm = 0.0;
        if (topology == 0) {
            for (int i = 0; i < n; ++i) {
                const auto [hRe, hIm] = stageEval(hz, stages[static_cast<std::size_t>(i)]);
                const double nr = tRe * hRe - tIm * hIm;
                const double ni = tRe * hIm + tIm * hRe;
                tRe = nr;
                tIm = ni;
            }
        } else if (topology == 2) {
            tRe = 0.0;
            tIm = 0.0;
            for (int i = 0; i < n; ++i) {
                const auto [hRe, hIm] = stageBpEval(hz, stages[static_cast<std::size_t>(i)]);
                const double sgn = (i & 1) == 0 ? invSqrtN : -invSqrtN;
                tRe += sgn * hRe;
                tIm += sgn * hIm;
            }
        } else {
            double aRe = 1.0, aIm = 0.0, bRe = 1.0, bIm = 0.0;
            for (int i = 0; i < n; ++i) {
                const auto [hRe, hIm] = stageEval(hz, stages[static_cast<std::size_t>(i)]);
                if ((i & 1) == 0) {
                    const double nr = aRe * hRe - aIm * hIm;
                    const double ni = aRe * hIm + aIm * hRe;
                    aRe = nr;
                    aIm = ni;
                } else {
                    const double nr = bRe * hRe - bIm * hIm;
                    const double ni = bRe * hIm + bIm * hRe;
                    bRe = nr;
                    bIm = ni;
                }
            }
            tRe = 0.5 * (aRe + bRe);
            tIm = 0.5 * (aIm + bIm);
        }
        const double mag2 = tRe * tRe + tIm * tIm;
        sumMag4 += mag2 * mag2;
    }
    const double meanMag4 = sumMag4 / static_cast<double>(kProbes.size());
    return std::pow(std::max(meanMag4, 1.0e-16), 0.25);
}

} // namespace

ResolvedLattice resolveLattice(int cubeIndex, float morphX, float morphY, float morphZ, float freqSemitones,
                               float resAmount, float transform, float stereoOffsetSt, const ImprintData* imprint,
                               int activeStages, int topology) noexcept PF_NONBLOCKING {
    const int ci = std::clamp(cubeIndex, 0, kNumCubes - 1);
    const CubeSpec& cube = cubeAt(ci);

    const auto corner = [&](int idx) -> const CornerSpec& {
        if (ci == kNumCubes - 1 && idx >= 0 && idx < kNumImprintSlots && imprint != nullptr &&
            imprint->slotEnabled(idx)) {
            return imprint->slotCorner(idx);
        }
        return cube.corners[static_cast<std::size_t>(idx)];
    };

    const double x = std::clamp(static_cast<double>(morphX), 0.0, 1.0);
    const double y = std::clamp(static_cast<double>(morphY), 0.0, 1.0);
    const double z = std::clamp(static_cast<double>(morphZ), 0.0, 1.0);

    const double clampedRes = std::clamp(static_cast<double>(resAmount), 0.0, 1.0);
    // Smooth resonance span from broad (0.30x) up to singing self-oscillating Q (4.6x, clamped <= 45).
    const double resScale = 0.30 * std::pow(15.3, clampedRes);
    const double xform = std::clamp(static_cast<double>(transform), -1.0, 1.0);

    ResolvedLattice out;
    const double cornerMakeupDb =
        linTrilerp(corner(0).makeupDb, corner(1).makeupDb, corner(2).makeupDb, corner(3).makeupDb, corner(4).makeupDb,
                   corner(5).makeupDb, corner(6).makeupDb, corner(7).makeupDb, x, y, z);

    for (std::size_t s = 0; s < kNumStages; ++s) {
        const auto pf = logTrilerp(corner(0).stages[s].poleHz, corner(1).stages[s].poleHz, corner(2).stages[s].poleHz,
                                   corner(3).stages[s].poleHz, corner(4).stages[s].poleHz, corner(5).stages[s].poleHz,
                                   corner(6).stages[s].poleHz, corner(7).stages[s].poleHz, x, y, z);
        const auto pq = logTrilerp(corner(0).stages[s].poleQ, corner(1).stages[s].poleQ, corner(2).stages[s].poleQ,
                                   corner(3).stages[s].poleQ, corner(4).stages[s].poleQ, corner(5).stages[s].poleQ,
                                   corner(6).stages[s].poleQ, corner(7).stages[s].poleQ, x, y, z);
        const auto zf = logTrilerp(corner(0).stages[s].zeroHz, corner(1).stages[s].zeroHz, corner(2).stages[s].zeroHz,
                                   corner(3).stages[s].zeroHz, corner(4).stages[s].zeroHz, corner(5).stages[s].zeroHz,
                                   corner(6).stages[s].zeroHz, corner(7).stages[s].zeroHz, x, y, z);
        const auto zq = logTrilerp(corner(0).stages[s].zeroQ, corner(1).stages[s].zeroQ, corner(2).stages[s].zeroQ,
                                   corner(3).stages[s].zeroQ, corner(4).stages[s].zeroQ, corner(5).stages[s].zeroQ,
                                   corner(6).stages[s].zeroQ, corner(7).stages[s].zeroQ, x, y, z);

        // Alternating binaural spread: even stages shift +stereoOffsetSt, odd stages -stereoOffsetSt,
        // creating balanced L/R spectral energy and wide 3D Z-plane immersion.
        const double stageStereoSt =
            ((s & 1u) == 0u) ? static_cast<double>(stereoOffsetSt) : -static_cast<double>(stereoOffsetSt);
        const double freqMul = std::pow(2.0, (static_cast<double>(freqSemitones) + stageStereoSt) / 12.0);

        const double ratio = zf / std::max(pf, 20.0);
        const double warpedRatio = std::pow(ratio, 1.0 + 0.85 * xform) * std::pow(2.0, 0.35 * xform);

        const double finalPoleHz = std::clamp(pf * freqMul, 30.0, 18500.0);
        const double finalZeroHz = std::clamp(finalPoleHz * warpedRatio, 30.0, 19500.0);
        const double finalPoleQ = std::clamp(pq * resScale, 0.55, 45.0);
        const double finalZeroQ = std::clamp(zq * (0.55 + 0.75 * clampedRes), 0.55, 24.0);

        out.stages[s] = {static_cast<float>(finalPoleHz), static_cast<float>(finalPoleQ),
                         static_cast<float>(finalZeroHz), static_cast<float>(finalZeroQ)};
    }

    // Analytical passband auto-makeup keeps every cube, corner, topology, stage count,
    // and user WAV imprint loudness-calibrated without high-Q sweep ripple.
    const double passbandEst = estimatePassbandGain(out.stages, activeStages, topology);
    const double maxBoost = (topology == 2) ? 3.2 : 2.0; // +10 dB cap for Parallel bandpass, +6 dB Serial/Hybrid
    const double autoGain = std::clamp(1.0 / std::max(passbandEst, 1.0e-5), 1.0e-5, maxBoost);
    const double resTrimDb = (topology == 2 ? -2.2 : -4.8) * std::max(0.0, clampedRes - 0.38);
    const double fineTrim = std::pow(10.0, (std::clamp(cornerMakeupDb, -6.0, 6.0) + resTrimDb) / 20.0);
    out.makeupLinear = static_cast<float>(autoGain * fineTrim);
    return out;
}

float evaluateMagnitudeDb(const ResolvedLattice& lattice, int activeStages, int topology, double hz,
                          double sampleRate) noexcept {
    const int n = std::clamp(activeStages, 1, kNumStages);
    const double fs = std::max(sampleRate, 22050.0);
    const double gEval = std::tan(kPi * std::clamp(hz, 10.0, 0.48 * fs) / fs);

    const auto stageH = [&](const SectionSpec& s) -> std::complex<double> {
        const double gp = std::tan(kPi * std::clamp(static_cast<double>(s.poleHz), 20.0, 0.47 * fs) / fs);
        const double gz = std::tan(kPi * std::clamp(static_cast<double>(s.zeroHz), 20.0, 0.47 * fs) / fs);
        const double kp = 1.0 / std::clamp(static_cast<double>(s.poleQ), 0.5, 45.0);
        const double kz = 1.0 / std::clamp(static_cast<double>(s.zeroQ), 0.5, 24.0);
        const double r = gz / gp;
        const double norm = 1.0 / std::max(0.25, r);
        const std::complex<double> sNorm(0.0, gEval / gp);
        const std::complex<double> s2 = sNorm * sNorm;
        const std::complex<double> num = norm * (s2 + (r * kz) * sNorm + r * r);
        const std::complex<double> den = s2 + kp * sNorm + 1.0;
        return num / den;
    };

    const auto stageBpH = [&](const SectionSpec& s) -> std::complex<double> {
        const double gp = std::tan(kPi * std::clamp(static_cast<double>(s.poleHz), 20.0, 0.47 * fs) / fs);
        const double kp = 1.0 / std::clamp(static_cast<double>(s.poleQ), 0.5, 45.0);
        const std::complex<double> sNorm(0.0, gEval / gp);
        return (std::sqrt(kp) * sNorm) / (sNorm * sNorm + kp * sNorm + 1.0);
    };

    std::complex<double> total(1.0, 0.0);
    if (topology == 0) {
        for (int i = 0; i < n; ++i)
            total *= stageH(lattice.stages[static_cast<std::size_t>(i)]);
    } else if (topology == 2) {
        total = {0.0, 0.0};
        const double invSqrtN = 1.0 / std::sqrt(static_cast<double>(n));
        for (int i = 0; i < n; ++i) {
            const double sign = (i & 1) == 0 ? invSqrtN : -invSqrtN;
            total += sign * stageBpH(lattice.stages[static_cast<std::size_t>(i)]);
        }
    } else {
        std::complex<double> a(1.0, 0.0), b(1.0, 0.0);
        for (int i = 0; i < n; ++i) {
            if ((i & 1) == 0)
                a *= stageH(lattice.stages[static_cast<std::size_t>(i)]);
            else
                b *= stageH(lattice.stages[static_cast<std::size_t>(i)]);
        }
        total = 0.5 * (a + b);
    }

    const double mag = std::abs(total) * static_cast<double>(lattice.makeupLinear);
    return static_cast<float>(20.0 * std::log10(std::max(mag, 1.0e-4)));
}

ZPlanePoint stageZPoint(const SectionSpec& s, double sampleRate) noexcept {
    const double fs = std::max(sampleRate, 22050.0);
    const auto bilinearRoot = [&](double hz, double q) -> std::pair<float, float> {
        const double g = std::tan(kPi * std::clamp(hz, 20.0, 0.47 * fs) / fs);
        const double k = 1.0 / std::clamp(q, 0.5, 45.0);
        const double disc = std::max(0.0, 1.0 - 0.25 * k * k);
        const std::complex<double> sc(-0.5 * g * k, g * std::sqrt(disc));
        const std::complex<double> z = (1.0 + sc) / (1.0 - sc);
        return {static_cast<float>(std::clamp(std::abs(z), 0.0, 0.999)),
                static_cast<float>(std::clamp(std::abs(std::arg(z)), 0.0, kPi))};
    };
    const auto [pr, pt] = bilinearRoot(s.poleHz, s.poleQ);
    const auto [zr, zt] = bilinearRoot(s.zeroHz, s.zeroQ);
    return {pr, pt, zr, zt};
}

} // namespace pf
