// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/engine.h"

#include <hba/dsp/oscillator.h>
#include <hba/dsp/pitch.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace tg {

namespace {
using hba::dsp::midiToHz;
using hba::dsp::polyBlep;

constexpr std::array<double, kVoiceCount> kBasePans{0.0, -0.78, 0.78, -0.42, 0.42};

// Strict zero-boundary Hann^2 log-frequency bell shaped by center & width.
inline double spiralBellWeight(double u, double octSpan, double centerOct, double widthNorm) noexcept HBA_NONBLOCKING {
    const double edgeNorm = std::clamp(u / octSpan, 0.0, 1.0);
    const double boundary = std::sin(hba::dsp::kPi * edgeNorm);
    const double boundarySq = boundary * boundary;
    const double sigma = 0.65 + std::clamp(widthNorm, 0.15, 1.0) * 2.25;
    const double dist = (u - centerOct) / sigma;
    const double tilt = std::exp(-0.5 * dist * dist);
    return boundarySq * tilt;
}

// Smooth C^1 zero-at-boundary guard so Sub-Anchor partials enter/exit wraps at exact 0.0 with zero click,
// while preserving 100% full club weight across the 38..110 Hz sub band.
inline double wrapSubBoundaryGuard(double u, double octSpan) noexcept HBA_NONBLOCKING {
    constexpr double kFadeOct = 0.48;
    const double lo = std::clamp(u / kFadeOct, 0.0, 1.0);
    const double hi = std::clamp((octSpan - u) / kFadeOct, 0.0, 1.0);
    const double sLo = std::sin(0.5 * hba::dsp::kPi * lo);
    const double sHi = std::sin(0.5 * hba::dsp::kPi * hi);
    return (sLo * sLo) * (sHi * sHi);
}

// Focused club Sub-Anchor Floor envelope centered around 52 Hz (30..92 Hz).
inline double subAnchorWeight(double hz) noexcept HBA_NONBLOCKING {
    if (hz < 18.0 || hz > 180.0)
        return 0.0;
    const double logDist = std::log2(hz / 52.0);
    return std::exp(-0.5 * (logDist / 0.44) * (logDist / 0.44));
}

inline double nyquistFade(double harmonicInc) noexcept HBA_NONBLOCKING {
    return std::clamp((0.40 - harmonicInc) / 0.12, 0.0, 1.0);
}
} // namespace

double Engine::oscSample(TimbreKind kind, double phase, double inc, double formantRatio, double foldAmt,
                         double pw) const noexcept HBA_NONBLOCKING {
    const double fund = std::sin(hba::dsp::kTwoPi * phase);
    switch (kind) {
    case TimbreKind::reese: {
        // Nyquist-aware PolyBLEP saw: crisp full saw in bass/mids, smooth crossfade to sine in high Shepard octaves.
        const double nyq = std::clamp((0.26 - inc) / 0.18, 0.0, 1.0);
        if (nyq <= 1e-4)
            return fund;
        double saw = 2.0 * phase - 1.0;
        saw -= polyBlep(phase, inc);
        return nyq * saw + (1.0 - nyq) * fund;
    }
    case TimbreKind::sine:
        return fund;
    case TimbreKind::formant: {
        // Lush dual-formant vocal choir: warm chest fundamental + 2nd harmonic anchored under a smooth
        // bandwidth-limited vocal resonance so chords stay velvety and never whistle at high cutoff.
        const double g2 = nyquistFade(2.0 * inc);
        const double body = 0.52 * fund + 0.18 * g2 * std::sin(2.0 * hba::dsp::kTwoPi * phase);
        const double window = 0.5 * (1.0 - std::cos(hba::dsp::kTwoPi * phase));
        const double f1 = std::clamp(formantRatio, 1.5, 6.2);
        const double maxH = std::max(1.0, std::min(f1, 0.34 / std::max(inc, 1e-6)));
        const double hLo = std::floor(maxH);
        const double hHi = hLo + 1.0;
        const double fracH = maxH - hLo;
        const double cLo = std::cos(hba::dsp::kTwoPi * hLo * phase);
        const double cHi = std::cos(hba::dsp::kTwoPi * hHi * phase);
        const double f1Fade = nyquistFade((maxH + 1.0) * inc);
        const double vocal = window * (cLo + fracH * (cHi - cLo));
        return body + 0.58 * f1Fade * vocal;
    }
    case TimbreKind::wavefold: {
        // Bandwidth-limited West-Coast FM wavefold: full snarl in bass/mids, zero fold-back aliasing in upper octaves.
        const double nyqFm = std::clamp((0.18 - inc) / 0.14, 0.0, 1.0);
        const double mod = (0.35 + foldAmt * 1.35) * nyqFm * std::sin(hba::dsp::kTwoPi * 2.0 * phase);
        return std::sin(hba::dsp::kTwoPi * phase + mod);
    }
    case TimbreKind::pulse: {
        const double nyq = std::clamp((0.24 - inc) / 0.16, 0.0, 1.0);
        if (nyq <= 1e-4)
            return 0.85 * fund;
        double p = phase < pw ? 1.0 : -1.0;
        p += polyBlep(phase, inc);
        p -= polyBlep(hba::dsp::frac(phase + 1.0 - pw), inc);
        return 0.85 * (nyq * p + (1.0 - nyq) * fund);
    }
    case TimbreKind::glassBell: {
        // Crystalline harmonic glass chime with strictly Nyquist-guarded upper partials & subtle FM shimmer.
        const double g2 = nyquistFade(2.0 * inc);
        const double g3 = nyquistFade(3.0 * inc);
        const double g4 = nyquistFade(4.0 * inc);
        const double fm = 0.42 * foldAmt * g3 * std::sin(3.0 * hba::dsp::kTwoPi * phase);
        return 0.62 * fund + 0.24 * g2 * std::sin(2.0 * hba::dsp::kTwoPi * phase + fm) +
               0.14 * g3 * std::sin(3.0 * hba::dsp::kTwoPi * phase) +
               0.08 * g4 * std::sin(4.0 * hba::dsp::kTwoPi * phase);
    }
    case TimbreKind::superOrgan: {
        // Classic Shepard-Risset tonewheel drawbar stack (8' + 4' + 2-2/3' + 2' + 1-1/3'), zero aliasing.
        const double g2 = nyquistFade(2.0 * inc);
        const double g3 = nyquistFade(3.0 * inc);
        const double g4 = nyquistFade(4.0 * inc);
        const double g6 = nyquistFade(6.0 * inc);
        return 0.50 * fund + 0.26 * g2 * std::sin(2.0 * hba::dsp::kTwoPi * phase) +
               0.15 * g3 * std::sin(3.0 * hba::dsp::kTwoPi * phase) +
               0.12 * g4 * std::sin(4.0 * hba::dsp::kTwoPi * phase) +
               0.07 * g6 * std::sin(6.0 * hba::dsp::kTwoPi * phase);
    }
    }
    return 0.0;
}

void Engine::renderSpiralSample(const ModeRenderArgs& a, double& sumBodyL, double& sumBodyR, double& sumSub,
                                double& lastEffectiveBellHz) noexcept HBA_NONBLOCKING {
    const double playedPc = std::fmod(a.rootMidi, 12.0);
    const double normPc = playedPc < 0.0 ? playedPc + 12.0 : playedPc;
    const double baseOctaveMidi = 12.0 + normPc; // ~16..31 Hz floor
    const double octaveShift = std::clamp((a.rootMidi - (36.0 + normPc)) / 12.0, -2.0, 2.5);
    const double effBellHz = std::clamp(a.curBellCenter * std::pow(2.0, octaveShift * 0.75), 65.0, 4800.0);
    lastEffectiveBellHz = effBellHz;
    const double bellCenterU =
        std::clamp((12.0 * std::log2(effBellHz / std::max(12.0, midiToHz(baseOctaveMidi)))) / a.ratioSemi, 0.75, 6.25);

    std::array<double, kOctaveCount> bellW{};
    std::array<double, kOctaveCount> partialU{};
    double bellPow = 0.0;
    for (int k = 0; k < kOctaveCount; ++k) {
        const auto ki = static_cast<std::size_t>(k);
        const double u = hba::dsp::frac((static_cast<double>(k) + a.curSpiralPos) / static_cast<double>(kOctaveCount)) *
                         static_cast<double>(kOctaveCount);
        partialU[ki] = u;
        const double b = spiralBellWeight(u, static_cast<double>(kOctaveCount), bellCenterU, a.bellWidth);
        bellW[ki] = b;
        bellPow += b * b;
    }
    const double rmsNorm = 1.0 / std::sqrt(std::max(0.35, bellPow));

    const int chordVoices = a.spiralChord.count;
    const double chordNorm = 1.0 / std::sqrt(static_cast<double>(chordVoices));

    for (int v = 0; v < chordVoices; ++v) {
        const auto vi = static_cast<std::size_t>(v);
        const double chordSemi = a.spiralChord.intervals[vi];
        const double pan = (chordVoices == 1) ? 0.0 : (kBasePans[vi] * a.width);
        const double panL = std::cos((pan + 1.0) * 0.25 * hba::dsp::kPi);
        const double panR = std::sin((pan + 1.0) * 0.25 * hba::dsp::kPi);

        for (int k = 0; k < kOctaveCount; ++k) {
            const auto ki = static_cast<std::size_t>(k);
            const double u = partialU[ki];
            const double midi = baseOctaveMidi + u * a.ratioSemi + chordSemi;
            const double hz = std::clamp(midiToHz(midi), 16.0, 0.44 * a.sr);
            const double bell = bellW[ki] * rmsNorm;

            if (v == 0 && a.captureTelemetry) {
                telemetry_.spiralPartialU[ki] = static_cast<float>(u);
                telemetry_.spiralPartialGain[ki] = static_cast<float>(bellW[ki]);
            }

            auto& ps = partials_[vi][ki];
            const double incA = (hz / a.detuneRatio) * a.invSr;
            const double incB = (hz * a.detuneRatio) * a.invSr;
            ps.phaseA = hba::dsp::frac(ps.phaseA + incA);
            ps.phaseB = hba::dsp::frac(ps.phaseB + incB);

            if (bell > 1e-5) {
                const double sA = oscSample(a.timbre, ps.phaseA, incA, a.formantRatio, a.foldAmt, a.pw);
                const double sB = oscSample(a.timbre, ps.phaseB, incB, a.formantRatio, a.foldAmt, a.pw);
                const double w = bell * 0.34 * chordNorm;
                const double mid = 0.5 * (sA + sB);
                const double side = 0.5 * (sA - sB) * a.width;
                sumBodyL += w * (mid * panL + side);
                sumBodyR += w * (mid * panR - side);
            }

            if (v == 0) {
                const double subW = subAnchorWeight(hz) * wrapSubBoundaryGuard(u, static_cast<double>(kOctaveCount));
                ps.phaseSub = hba::dsp::frac(ps.phaseSub + hz * a.invSr);
                if (subW > 1e-5)
                    sumSub += subW * std::sin(hba::dsp::kTwoPi * ps.phaseSub) * 0.62;
            }
        }
    }
}

void Engine::renderCopedentSample(const ModeRenderArgs& a, double& sumBodyL, double& sumBodyR,
                                  double& sumSub) noexcept HBA_NONBLOCKING {
    constexpr double voiceNorm = 0.30;
    for (int v = 0; v < kVoiceCount; ++v) {
        const auto vi = static_cast<std::size_t>(v);
        const double rampedPitch = voicePitchRamp_[vi].next();
        const double smoothedPitch = voicePitchSmooth_[vi].step(rampedPitch, a.voicePitchCoef);
        const double hz = std::clamp(midiToHz(smoothedPitch), 20.0, 0.44 * a.sr);

        auto& ps = partials_[vi][0];
        const double incA = (hz / a.detuneRatio) * a.invSr;
        const double incB = (hz * a.detuneRatio) * a.invSr;
        ps.phaseA = hba::dsp::frac(ps.phaseA + incA);
        ps.phaseB = hba::dsp::frac(ps.phaseB + incB);

        const double sA = oscSample(a.timbre, ps.phaseA, incA, a.formantRatio, a.foldAmt, a.pw);
        const double sB = oscSample(a.timbre, ps.phaseB, incB, a.formantRatio, a.foldAmt, a.pw);
        const double pan = kBasePans[vi] * a.width;
        const double panL = std::cos((pan + 1.0) * 0.25 * hba::dsp::kPi);
        const double panR = std::sin((pan + 1.0) * 0.25 * hba::dsp::kPi);
        const double mid = 0.5 * (sA + sB);
        const double side = 0.5 * (sA - sB) * a.width * 0.65;
        const double gainV = (v == 0 ? 1.15 : 0.92) * voiceNorm;

        sumBodyL += gainV * (mid * panL + side);
        sumBodyR += gainV * (mid * panR - side);

        if (v == 0) {
            ps.phaseSub = hba::dsp::frac(ps.phaseSub + hz * a.invSr);
            sumSub += std::sin(hba::dsp::kTwoPi * ps.phaseSub) * 0.55;
        }
    }
}

void Engine::renderMobiusSample(const ModeRenderArgs& a, double& sumBodyL, double& sumBodyR,
                                double& sumSub) noexcept HBA_NONBLOCKING {
    constexpr int kMobiusCopies = 4;
    constexpr double kMobiusSpanSemi = 48.0; // 4 octaves
    const double floorMidi = std::clamp(a.rootMidi - 12.0, 20.0, 48.0);
    const double bellCenterU =
        std::clamp((12.0 * std::log2(a.curBellCenter / std::max(16.0, midiToHz(floorMidi)))) / 12.0, 0.7, 3.3);
    constexpr double voiceNorm = 0.20;

    for (int v = 0; v < kVoiceCount; ++v) {
        const auto vi = static_cast<std::size_t>(v);
        const double rampedPitch = voicePitchRamp_[vi].next();
        const double smoothedPitch = voicePitchSmooth_[vi].step(rampedPitch, a.voicePitchCoef);
        const double pan = kBasePans[vi] * a.width;
        const double panL = std::cos((pan + 1.0) * 0.25 * hba::dsp::kPi);
        const double panR = std::sin((pan + 1.0) * 0.25 * hba::dsp::kPi);

        for (int c = 0; c < kMobiusCopies; ++c) {
            const auto ci = static_cast<std::size_t>(c);
            const double relSemi = smoothedPitch - floorMidi + static_cast<double>(c) * 12.0;
            const double wrappedSemi = hba::dsp::frac(relSemi / kMobiusSpanSemi) * kMobiusSpanSemi;
            const double uOct = wrappedSemi / 12.0;
            const double midi = floorMidi + wrappedSemi;
            const double hz = std::clamp(midiToHz(midi), 20.0, 0.44 * a.sr);
            const double bell = spiralBellWeight(uOct, static_cast<double>(kMobiusCopies), bellCenterU, a.bellWidth);

            if (v == 0 && a.captureTelemetry) {
                telemetry_.spiralPartialU[ci] = static_cast<float>(uOct * (7.0 / 4.0));
                telemetry_.spiralPartialGain[ci] = static_cast<float>(bell);
            }

            auto& ps = partials_[vi][ci];
            const double incA = (hz / a.detuneRatio) * a.invSr;
            const double incB = (hz * a.detuneRatio) * a.invSr;
            ps.phaseA = hba::dsp::frac(ps.phaseA + incA);
            ps.phaseB = hba::dsp::frac(ps.phaseB + incB);

            if (bell > 1e-5) {
                const double sA = oscSample(a.timbre, ps.phaseA, incA, a.formantRatio, a.foldAmt, a.pw);
                const double sB = oscSample(a.timbre, ps.phaseB, incB, a.formantRatio, a.foldAmt, a.pw);
                const double w = bell * voiceNorm;
                const double mid = 0.5 * (sA + sB);
                const double side = 0.5 * (sA - sB) * a.width * 0.55;
                sumBodyL += w * (mid * panL + side);
                sumBodyR += w * (mid * panR - side);
            }
            if (v == 0) {
                // Zero-at-boundary sub guard eliminates octave-wrap sub thump in Mobius!
                const double subW =
                    subAnchorWeight(hz) * wrapSubBoundaryGuard(uOct, static_cast<double>(kMobiusCopies));
                ps.phaseSub = hba::dsp::frac(ps.phaseSub + hz * a.invSr);
                if (subW > 1e-5)
                    sumSub += subW * std::sin(hba::dsp::kTwoPi * ps.phaseSub) * 0.52;
            }
        }
    }
}

} // namespace tg
