// SPDX-License-Identifier: GPL-3.0-or-later
#include "pf/cubes.h"

#include <hba/dsp/crossover.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace pf {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

// -----------------------------------------------------------------------------
// Offline 14th-order Burg's Method LPC + Durand-Kerner complex root finder.
// 1. Removes DC offset from the analysis frame.
// 2. Anti-alias filters (LR4 LP) and decimates any sample rate (44.1..192 kHz)
//    down to ~22-24 kHz so all 14 poles model audible formants without aliasing.
// 3. Scores sustained tonal frames (down-weighting impulsive transient spikes).
// 4. Rejects real-axis roots and deduplicates near-identical formants.
// 5. Pads any missing slots from non-overlapping fallback bands (never stacking
//    duplicate poles) and places alternating dipole zeros for flat shelf balance.
// -----------------------------------------------------------------------------
CornerSpec imprintFromAudio(std::span<const float> monoSamples, double sampleRate) {
    const CornerSpec fallback = cubeAt(kNumCubes - 1).corners[0];
    if (monoSamples.size() < 64 || sampleRate < 8000.0)
        return fallback;

    // Choose integer decimation factor to land effFs in ~16..28 kHz for any rate up to 192 kHz.
    const int decim = std::clamp(static_cast<int>(std::lround(sampleRate / 24000.0)), 1, 8);
    const double effFs = sampleRate / static_cast<double>(decim);

    // Target ~42 ms analysis window (~1024 decimated samples at 24 kHz).
    const std::size_t targetWin = static_cast<std::size_t>(std::clamp(std::lround(sampleRate * 0.0426), 128L, 8192L));
    const std::size_t winLen = std::min(targetWin, monoSamples.size());
    const std::size_t hop = std::max<std::size_t>(1, winLen / 4);

    const auto safeSample = [](float s) { return std::isfinite(s) ? static_cast<double>(s) : 0.0; };

    // Pick highest sustained tonal energy frame (penalising sharp transient clicks via crest factor).
    std::size_t bestStart = 0;
    double bestScore = 0.0;
    std::size_t maxRmsStart = 0;
    double maxRms = 0.0;
    for (std::size_t start = 0; start + winLen <= monoSamples.size(); start += hop) {
        double mean = 0.0;
        for (std::size_t i = 0; i < winLen; ++i)
            mean += safeSample(monoSamples[start + i]);
        mean /= static_cast<double>(winLen);

        double energy = 0.0;
        double peak = 0.0;
        for (std::size_t i = 0; i < winLen; ++i) {
            const double v = safeSample(monoSamples[start + i]) - mean;
            energy += v * v;
            peak = std::max(peak, std::abs(v));
        }
        const double rms = std::sqrt(energy / static_cast<double>(winLen));
        if (rms > maxRms) {
            maxRms = rms;
            maxRmsStart = start;
        }
        if (rms < 1.0e-5)
            continue;
        const double crest = peak / rms;
        // Prefer frames with moderate crest factor (voiced/sustained body over brief impulse spikes).
        const double sustainWeight = 1.0 / (1.0 + std::max(0.0, crest - 3.5) * 0.45);
        const double score = rms * sustainWeight;
        if (score > bestScore) {
            bestScore = score;
            bestStart = start;
        }
    }
    if (maxRms < 1.0e-5)
        return fallback;
    if (bestScore <= 0.0)
        bestStart = maxRmsStart;

    // Remove DC offset and anti-alias filter before decimation (with short pre-roll if available).
    double frameMean = 0.0;
    for (std::size_t i = 0; i < winLen; ++i)
        frameMean += safeSample(monoSamples[bestStart + i]);
    frameMean /= static_cast<double>(winLen);

    const std::size_t n = winLen / static_cast<std::size_t>(decim);
    if (n < 32)
        return fallback;

    std::vector<double> decimated(n, 0.0);
    if (decim > 1) {
        hba::dsp::Lr4Lowpass aaLp;
        const auto aaCoefs = hba::dsp::butterworth(0.42 * effFs, sampleRate);
        const std::size_t preRoll = std::min<std::size_t>(bestStart, static_cast<std::size_t>(decim) * 16);
        for (std::size_t i = 0; i < preRoll; ++i)
            (void)aaLp.process(aaCoefs, safeSample(monoSamples[bestStart - preRoll + i]) - frameMean);
        for (std::size_t i = 0; i < n; ++i) {
            double sample = 0.0;
            for (int d = 0; d < decim; ++d) {
                const double raw =
                    safeSample(
                        monoSamples[bestStart + i * static_cast<std::size_t>(decim) + static_cast<std::size_t>(d)]) -
                    frameMean;
                sample = aaLp.process(aaCoefs, raw);
            }
            decimated[i] = sample;
        }
    } else {
        for (std::size_t i = 0; i < n; ++i)
            decimated[i] = safeSample(monoSamples[bestStart + i]) - frameMean;
    }

    // Gentle pre-emphasis + Hann window.
    std::vector<double> x(n, 0.0);
    double prev = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double pre = decimated[i] - 0.65 * prev;
        prev = decimated[i];
        const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n - 1));
        x[i] = pre * hann;
    }

    constexpr int P = 2 * kNumStages; // order 14
    std::vector<double> ef = x;
    std::vector<double> eb = x;
    std::array<double, P + 1> a{};
    a[0] = 1.0;

    for (int m = 1; m <= P; ++m) {
        double num = 0.0, den = 0.0;
        for (std::size_t i = static_cast<std::size_t>(m); i < n; ++i) {
            num += ef[i] * eb[i - 1];
            den += ef[i] * ef[i] + eb[i - 1] * eb[i - 1];
        }
        if (den < 1.0e-18)
            break;
        const double km = std::clamp(-2.0 * num / den, -0.992, 0.992);

        std::array<double, P + 1> nextA = a;
        for (int j = 1; j < m; ++j)
            nextA[static_cast<std::size_t>(j)] =
                a[static_cast<std::size_t>(j)] + km * a[static_cast<std::size_t>(m - j)];
        nextA[static_cast<std::size_t>(m)] = km;
        a = nextA;

        for (std::size_t i = n - 1; i >= static_cast<std::size_t>(m); --i) {
            const double f = ef[i] + km * eb[i - 1];
            const double b = eb[i - 1] + km * ef[i];
            ef[i] = f;
            eb[i] = b;
        }
    }

    // Solve z^P + a[1]*z^(P-1) + ... + a[P] = 0 via Durand-Kerner iteration.
    using C = std::complex<double>;
    std::array<C, P> roots{};
    for (int i = 0; i < P; ++i) {
        const double angle = 2.0 * kPi * (static_cast<double>(i) + 0.37) / static_cast<double>(P);
        roots[static_cast<std::size_t>(i)] = std::polar(0.82, angle);
    }
    const auto evalPoly = [&](C z) -> C {
        C v(1.0, 0.0);
        for (int j = 1; j <= P; ++j)
            v = v * z + a[static_cast<std::size_t>(j)];
        return v;
    };
    for (int iter = 0; iter < 120; ++iter) {
        double maxStep = 0.0;
        for (int i = 0; i < P; ++i) {
            const C zi = roots[static_cast<std::size_t>(i)];
            C denom(1.0, 0.0);
            for (int j = 0; j < P; ++j) {
                if (i == j)
                    continue;
                denom *= (zi - roots[static_cast<std::size_t>(j)]);
            }
            if (std::abs(denom) < 1.0e-18)
                continue;
            const C delta = evalPoly(zi) / denom;
            roots[static_cast<std::size_t>(i)] -= delta;
            maxStep = std::max(maxStep, std::abs(delta));
        }
        if (maxStep < 1.0e-10)
            break;
    }

    // Strictly extract upper-half-plane complex conjugate poles (reject real-axis tilt roots!).
    struct Extracted {
        double hz;
        double q;
    };
    std::vector<Extracted> poles;
    poles.reserve(kNumStages);
    for (const C& r : roots) {
        if (!std::isfinite(r.real()) || !std::isfinite(r.imag()) || r.imag() <= 1.0e-3)
            continue;
        const double radius = std::clamp(std::abs(r), 0.25, 0.992);
        const double theta = std::arg(r);
        if (!std::isfinite(theta) || theta < 0.012 || theta > kPi * 0.95)
            continue;
        const double hz = std::clamp(theta * effFs / (2.0 * kPi), 55.0, 14000.0);
        const double bw = std::max(18.0, -std::log(radius) * effFs / kPi);
        const double q = std::clamp(hz / bw, 1.3, 12.0);
        bool tooClose = false;
        for (const auto& existing : poles) {
            if (std::abs(existing.hz - hz) / std::max(existing.hz, hz) < 0.06) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose)
            poles.push_back({hz, q});
    }

    if (poles.empty())
        return fallback;

    // Pad gracefully with non-overlapping fallback bands (NEVER duplicate existing poles!).
    for (const auto& fb : fallback.stages) {
        if (static_cast<int>(poles.size()) >= kNumStages)
            break;
        const double fHz = static_cast<double>(fb.poleHz);
        bool overlaps = false;
        for (const auto& existing : poles) {
            if (std::abs(existing.hz - fHz) / std::max(existing.hz, fHz) < 0.18) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps)
            poles.push_back({fHz, std::clamp(static_cast<double>(fb.poleQ) * 0.55, 1.4, 3.2)});
    }
    // Final geometric grid fill if audio had ultra-dense clusters matching all fallback bands.
    constexpr std::array<double, kNumStages> kGridHz{180.0, 420.0, 920.0, 1950.0, 3800.0, 6800.0, 11200.0};
    for (double gHz : kGridHz) {
        if (static_cast<int>(poles.size()) >= kNumStages)
            break;
        bool overlaps = false;
        for (const auto& existing : poles) {
            if (std::abs(existing.hz - gHz) / std::max(existing.hz, gHz) < 0.12) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps)
            poles.push_back({gHz, 1.6});
    }

    std::sort(poles.begin(), poles.end(), [](const Extracted& u, const Extracted& v) { return u.hz < v.hz; });
    if (static_cast<int>(poles.size()) > kNumStages)
        poles.resize(kNumStages);

    // Place alternating dipole zeros (even stages above pole, odd stages below pole)
    // so cumulative shelf tilt cancels out cleanly across all 7 imprinted stages.
    CornerSpec result{};
    result.makeupDb = 0.0f;
    for (int i = 0; i < kNumStages; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const Extracted& p = poles[idx];
        double zeroHz = ((i & 1) == 0) ? p.hz * 1.28 : p.hz * 0.78;
        if ((i & 1) == 0 && i + 1 < kNumStages) {
            const double nextHz = poles[idx + 1].hz;
            if (nextHz > p.hz * 1.12)
                zeroHz = std::clamp(std::sqrt(p.hz * nextHz), p.hz * 1.12, p.hz * 1.65);
        } else if ((i & 1) == 1 && i > 0) {
            const double prevHz = poles[idx - 1].hz;
            if (p.hz > prevHz * 1.12)
                zeroHz = std::clamp(std::sqrt(p.hz * prevHz), p.hz * 0.60, p.hz * 0.89);
        }
        result.stages[idx] = {static_cast<float>(std::clamp(p.hz, 55.0, 14000.0)),
                              static_cast<float>(std::clamp(p.q, 1.3, 11.5)),
                              static_cast<float>(std::clamp(zeroHz, 45.0, 16500.0)),
                              static_cast<float>(std::clamp(p.q * 0.78, 1.3, 9.0))};
    }
    return result;
}
} // namespace pf
