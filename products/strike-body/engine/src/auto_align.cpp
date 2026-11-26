// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sb/engine.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace sb {

AlignResult solveAutoAlign(const Params& baseParams, double sampleRate) {
    AlignResult res;
    res.bodyPhaseDeg = baseParams.real(ParamId::bodyPhase);
    res.shellOffsetMs = baseParams.real(ParamId::shellOffsetMs);
    res.shellInvert = baseParams.flag(ParamId::shellInvert);
    res.wireOffsetMs = baseParams.real(ParamId::wireOffsetMs);
    res.wireInvert = baseParams.flag(ParamId::wireInvert);

    const double sr = sampleRate > 8000.0 ? sampleRate : 48000.0;
    const int winSamples = std::max(512, static_cast<int>(std::lround(0.018 * sr)));
    const double lpAlpha = 1.0 - std::exp(-hba::dsp::kTwoPi * 700.0 / sr);

    auto renderLowBandLayer = [&](const Params& p, double bLvl, double sLvl, double wLvl) {
        Params lp = p;
        lp.set(ParamId::bodyLevel, static_cast<float>(bLvl));
        lp.set(ParamId::shellLevel, static_cast<float>(sLvl));
        lp.set(ParamId::wireLevel, static_cast<float>(wLvl));
        lp.set(ParamId::bodyMute, 0.0f);
        lp.set(ParamId::shellMute, 0.0f);
        lp.set(ParamId::wireMute, 0.0f);
        lp.set(ParamId::layerSolo, 0.0f);
        lp.set(ParamId::shellOffsetMs, 0.0f);
        lp.set(ParamId::wireOffsetMs, 0.0f);
        lp.set(ParamId::shellInvert, 0.0f);
        lp.set(ParamId::wireInvert, 0.0f);
        lp.set(ParamId::humanize, 0.0f);
        lp.set(ParamId::drive, 0.0f);
        lp.set(ParamId::disperserAmt, 0.0f);
        lp.set(ParamId::roomAmt, 0.0f);
        lp.set(ParamId::scoopAmt, 0.0f);
        lp.set(ParamId::limiter, 0.0f);
        lp.set(ParamId::outputDb, 0.0f);

        auto eng = std::make_unique<Engine>();
        eng->prepare(sr, winSamples, 2);
        const int lat = eng->latencySamples();
        const int total = winSamples + lat;
        std::vector<float> tmpL(static_cast<std::size_t>(total));
        std::vector<float> tmpR(static_cast<std::size_t>(total));
        eng->process(lp, nullptr, nullptr, 0);
        eng->trigger(Articulation::main, 1.0f, 1.0f);
        eng->process(lp, tmpL.data(), tmpR.data(), total);

        std::vector<double> lo(static_cast<std::size_t>(winSamples));
        double lpState = 0.0;
        for (int i = 0; i < winSamples; ++i) {
            const double m = 0.5 * (tmpL[static_cast<std::size_t>(i + lat)] + tmpR[static_cast<std::size_t>(i + lat)]);
            lpState += lpAlpha * (m - lpState);
            lo[static_cast<std::size_t>(i)] = lpState;
        }
        return lo;
    };

    const auto evalLock = [&](const std::vector<double>& a, const std::vector<double>& b, int shiftSamples,
                              double sign) {
        double dot = 0.0, eA = 0.0, eB = 0.0;
        for (int i = 0; i < winSamples; ++i) {
            const int j = i - shiftSamples;
            const double va = a[static_cast<std::size_t>(i)];
            const double vb = (j >= 0 && j < winSamples) ? (b[static_cast<std::size_t>(j)] * sign) : 0.0;
            const double w = std::exp(-static_cast<double>(i) / (0.008 * sr));
            dot += w * va * vb;
            eA += w * va * va;
            eB += w * vb * vb;
        }
        const double r = (eA > 1e-10 && eB > 1e-10) ? std::clamp(dot / std::sqrt(eA * eB), -1.0, 1.0) : 0.0;
        return 50.0 + 50.0 * r;
    };

    const auto shellWave =
        renderLowBandLayer(baseParams, 0.0, std::max(25.0f, baseParams.real(ParamId::shellLevel)), 0.0);
    const auto wireWave =
        renderLowBandLayer(baseParams, 0.0, 0.0, std::max(25.0f, baseParams.real(ParamId::wireLevel)));
    const auto initBody =
        renderLowBandLayer(baseParams, std::max(25.0f, baseParams.real(ParamId::bodyLevel)), 0.0, 0.0);

    const int initShellShift = static_cast<int>(std::lround(res.shellOffsetMs * 0.001 * sr));
    res.beforeLockPct = static_cast<float>(evalLock(initBody, shellWave, initShellShift, res.shellInvert ? -1.0 : 1.0));

    // Grid-locked search: shellOffsetMs in [0 .. +3.2 ms] + polarity invert spans ALL 360 deg of phase
    // with ZERO timing delay added to Body!
    const int maxShift = std::max(1, static_cast<int>(std::lround(0.0032 * sr)));
    const int stepShift = std::max(1, static_cast<int>(std::lround(0.00010 * sr))); // 0.10 ms precision

    double bestScore = -10.0;
    double bestPhase = res.bodyPhaseDeg;
    int bestShellShift = 0;
    bool bestShellInv = false;
    std::vector<double> bestBody = initBody;

    for (int pIdx = 0; pIdx < 16; ++pIdx) {
        const float candPhase = static_cast<float>(pIdx) * 22.5f;
        Params bp = baseParams;
        bp.set(ParamId::bodyPhase, candPhase);
        const auto candBody = renderLowBandLayer(bp, std::max(25.0f, baseParams.real(ParamId::bodyLevel)), 0.0, 0.0);

        for (int sh = 0; sh <= maxShift; sh += stepShift) {
            for (int inv = 0; inv < 2; ++inv) {
                const double sgn = inv ? -1.0 : 1.0;
                const double lockPct = evalLock(candBody, shellWave, sh, sgn);
                const double penalty = 1.5 * (static_cast<double>(sh) / static_cast<double>(maxShift));
                if (lockPct - penalty > bestScore) {
                    bestScore = lockPct - penalty;
                    bestPhase = candPhase;
                    bestShellShift = sh;
                    bestShellInv = (inv != 0);
                    bestBody = candBody;
                }
            }
        }
    }

    // Align wire/click layer grid-locked in [0 .. +2.0 ms] as well
    const int maxWireShift = std::max(1, static_cast<int>(std::lround(0.0020 * sr)));
    double bestWireScore = -10.0;
    int bestWireShift = 0;
    bool bestWireInv = false;
    for (int sh = 0; sh <= maxWireShift; sh += stepShift) {
        for (int inv = 0; inv < 2; ++inv) {
            const double sgn = inv ? -1.0 : 1.0;
            const double lockPct = evalLock(bestBody, wireWave, sh, sgn);
            const double penalty = 2.0 * (static_cast<double>(sh) / static_cast<double>(maxWireShift));
            if (lockPct - penalty > bestWireScore) {
                bestWireScore = lockPct - penalty;
                bestWireShift = sh;
                bestWireInv = (inv != 0);
            }
        }
    }

    res.bodyPhaseDeg = static_cast<float>(bestPhase);
    res.shellOffsetMs = std::clamp(static_cast<float>(static_cast<double>(bestShellShift) * 1000.0 / sr), 0.0f, 5.0f);
    res.shellInvert = bestShellInv;
    res.wireOffsetMs = std::clamp(static_cast<float>(static_cast<double>(bestWireShift) * 1000.0 / sr), 0.0f, 5.0f);
    res.wireInvert = bestWireInv;
    res.afterLockPct = static_cast<float>(evalLock(bestBody, shellWave, bestShellShift, bestShellInv ? -1.0 : 1.0));
    return res;
}

} // namespace sb
