// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engine_helpers.h"
#include "sb/engine.h"

#include <algorithm>
#include <cmath>

namespace sb {

double Engine::nextNoise(std::uint32_t& state) noexcept HBA_NONBLOCKING {
    state = state * 1664525u + 1013904223u;
    return static_cast<double>(state) / 2147483648.0 - 1.0;
}

void Engine::startVoice(Voice& v, Articulation art, double vel, double pitchRatio, int midiNote,
                        const Params& params) noexcept HBA_NONBLOCKING {
    ++triggerSeq_;
    v = {};
    v.active = true;
    v.midiNote = midiNote;
    v.art = art;
    v.velocity = std::clamp(vel, 0.05, 1.0);
    v.pitchRatio = std::clamp(pitchRatio, 0.125, 8.0);
    v.ageSamples = 0.0;
    v.fadeOutGain = 1.0;
    v.bodyFadeGain = 1.0;
    v.stealing = false;
    v.bodyStealing = false;

    const double hum = std::clamp(static_cast<double>(params.real(ParamId::humanize)) * 0.01, 0.0, 1.0);
    const std::uint32_t seedOffset = (hum > 1e-4) ? (triggerSeq_ * 2654435761u) : 0u;
    v.rngL = 0x51a7c3d1u ^ seedOffset;
    v.rngR = 0x9e3779b9u ^ (seedOffset * 3u);

    if (hum > 1e-4) {
        const double r1 = nextNoise(v.rngL);
        const double r2 = nextNoise(v.rngR);
        const double r3 = nextNoise(v.rngL);
        v.humPitchMult = detail::semitoneRatio(r1 * 0.35 * hum);
        v.humDecayMult = std::clamp(1.0 + r2 * 0.14 * hum, 0.75, 1.30);
        v.humStrikeOff = r3 * 0.10 * hum;
    } else {
        v.humPitchMult = 1.0;
        v.humDecayMult = 1.0;
        v.humStrikeOff = 0.0;
    }

    const detail::ArtProfile prof = detail::profileFor(art);
    const double mTight = std::clamp(static_cast<double>(params.real(ParamId::macroTight)) * 0.01, -1.0, 1.0);
    const double mMetal = std::clamp(static_cast<double>(params.real(ParamId::macroMetal)) * 0.01, 0.0, 1.0);
    const double decayScale = detail::tightDecayScale(mTight);

    // Layer 1: Phase-locked Body starts at exact phase phi_0 (0..360 deg -> 0..1 cycles)
    v.bodyPhase = std::clamp(static_cast<double>(params.real(ParamId::bodyPhase)) / 360.0, 0.0, 1.0);
    v.fmPhase = 0.0;
    v.bodyEnv = 1.0;
    v.pitchEnv = 1.0;

    // Layer 2: Initialize 24 nonlinear modal oscillators from physical shell geometry + Metal macro
    const auto model = static_cast<ShellModel>(std::clamp(params.choice(ParamId::shellModel), 0, 5));
    v.malletType = static_cast<MalletType>(std::clamp(params.choice(ParamId::malletType), 0, 5));
    const ModalGeometry& geom = modalGeometry(model);

    const double bodyHz =
        std::clamp(static_cast<double>(params.real(ParamId::bodyHz)), 32.0, 360.0) * v.pitchRatio * v.humPitchMult;
    const double shellTuneSt = static_cast<double>(params.real(ParamId::shellTune)) + prof.shellPitchSt + mMetal * 3.5;
    const double shellRootHz = std::clamp(bodyHz * detail::semitoneRatio(shellTuneSt), 45.0, 4400.0);
    const double stretch =
        std::clamp(static_cast<double>(params.real(ParamId::shellStretch)) * 0.01 + mMetal * 0.28, -0.5, 1.0);
    const double strikePos = std::clamp(static_cast<double>(params.real(ParamId::strikePos)) * 0.01 + prof.strikeShift +
                                            v.humStrikeOff + mMetal * 0.34,
                                        0.0, 1.0);
    const double baseDecaySec = std::max(0.018, static_cast<double>(params.real(ParamId::shellDecay)) * 0.001 *
                                                    decayScale * (1.0 + 0.35 * mMetal)) *
                                v.humDecayMult;
    const double hiDamp =
        std::clamp(static_cast<double>(params.real(ParamId::shellDamp)) * 0.01 - mMetal * 0.28, 0.0, 1.0);
    const double spread =
        std::clamp(static_cast<double>(params.real(ParamId::shellSpread)) * 0.01 + mMetal * 0.18, 0.0, 1.0);

    const double maxSafeHz = 0.43 * sampleRate_;
    double weightSum = 0.0;
    for (int k = 0; k < kModeCount; ++k) {
        const auto ki = static_cast<std::size_t>(k);
        auto& m = v.modes[ki];
        const double ratio = stretchedRatio(geom.ratios[ki], stretch);
        const double freq = shellRootHz * ratio;
        m.baseFreqHz = freq;
        m.re = 0.0;
        m.im = 0.0;

        if (freq >= maxSafeHz) {
            m.excWeight = 0.0;
            m.decayCoef = 0.9;
            continue;
        }

        const double w = hba::dsp::kTwoPi * freq / sampleRate_;
        m.cosW = std::cos(w);
        m.sinW = std::sin(w);

        const double freqScale = std::pow(std::max(1.0, ratio), 0.30 + 0.96 * hiDamp);
        const double modeTau = std::max(0.004, baseDecaySec / freqScale);
        m.decayCoef = std::exp(-1.0 / (modeTau * sampleRate_));

        const double exc = strikeModeWeight(geom.gains[ki], geom.radialOrder[ki], geom.angularOrder[ki], strikePos);
        m.excWeight = exc;
        weightSum += exc * exc;

        const double pan = (k < 2) ? 0.0 : (((k & 1) ? 1.0 : -1.0) * spread * std::min(1.0, 0.35 + 0.05 * k));
        m.gainL = std::cos((pan + 1.0) * 0.25 * hba::dsp::kPi);
        m.gainR = std::sin((pan + 1.0) * 0.25 * hba::dsp::kPi);

        telemetry_.modeRadial[ki] = static_cast<float>(geom.radialOrder[ki]);
        telemetry_.modeAngular[ki] = geom.angularOrder[ki];
    }
    const double normShell = 1.0 / std::sqrt(std::max(0.5, weightSum));
    for (auto& m : v.modes)
        m.excWeight *= normShell;

    v.shellEnergy = 1.0;
    v.malletChirpPhase = 0.0;

    // Layer 3: Wire + Click exciter
    v.wireEnv = 1.0;
    v.clickEnv = 1.0;
    v.duckEnv = 1.0;
    v.crushHoldL = 0.0;
    v.crushHoldR = 0.0;
    v.crushPhase = 0.0;

    // Start fresh latency-compensated 40 ms Transient Microscope capture!
    capturingScope_ = true;
    scopeCaptureSample_ = 0;
    scopeRmsSum_ = 0.0;
    scopePeakAbs_ = 0.0;
    lockBandBodySq_ = 0.0;
    lockBandOtherSq_ = 0.0;
    lockBandSumSq_ = 0.0;
    lockLpBody_ = 0.0;
    lockLpOther_ = 0.0;

    telemetry_.lastFundamentalHz = static_cast<float>(bodyHz);
    telemetry_.lastVelocity = static_cast<float>(v.velocity);
    telemetry_.lastArticulation = static_cast<int>(art);
    telemetry_.triggerCount = triggerSeq_;
}

void Engine::trigger(Articulation art, float velocity, float pitchRatio) noexcept HBA_NONBLOCKING {
    int chosen = -1;
    double oldestAge = -1.0;
    for (int i = 0; i < kMaxVoices; ++i) {
        if (!voices_[static_cast<std::size_t>(i)].active) {
            chosen = i;
            break;
        }
        if (voices_[static_cast<std::size_t>(i)].ageSamples > oldestAge) {
            oldestAge = voices_[static_cast<std::size_t>(i)].ageSamples;
            chosen = i;
        }
    }

    const auto choke = static_cast<ChokeMode>(std::clamp(cachedParams_.choice(ParamId::chokeMode), 0, 2));
    for (int i = 0; i < kMaxVoices; ++i) {
        if (i == chosen || !voices_[static_cast<std::size_t>(i)].active)
            continue;
        if (choke == ChokeMode::monoCut) {
            voices_[static_cast<std::size_t>(i)].stealing = true;
        } else if (choke == ChokeMode::splitSub) {
            voices_[static_cast<std::size_t>(i)].bodyStealing = true;
        }
    }

    startVoice(voices_[static_cast<std::size_t>(chosen)], art, static_cast<double>(velocity),
               static_cast<double>(pitchRatio), -1, cachedParams_);
}

void Engine::noteOn(int midiNote, float velocity, const Params& params) noexcept HBA_NONBLOCKING {
    cachedParams_ = params;
    havePendingParams_ = true;

    const auto mode = static_cast<KeyTrackMode>(std::clamp(params.choice(ParamId::keyTrack), 0, 1));
    if (mode == KeyTrackMode::chromatic) {
        const double ratio = detail::semitoneRatio(static_cast<double>(midiNote - 60));
        trigger(Articulation::main, velocity, static_cast<float>(ratio));
        return;
    }

    const int pc = ((midiNote % 12) + 12) % 12;
    Articulation art = Articulation::main;
    if (pc == 1)
        art = Articulation::ghost;
    else if (pc == 2)
        art = Articulation::rim;
    else if (pc == 3 || pc == 4)
        art = Articulation::roll;
    else if (velocity < 0.36f)
        art = Articulation::ghost;

    trigger(art, velocity, 1.0f);
}

void Engine::noteOff(int) noexcept HBA_NONBLOCKING {}

void Engine::allNotesOff() noexcept HBA_NONBLOCKING {
    for (auto& v : voices_)
        v.stealing = true;
}

} // namespace sb
