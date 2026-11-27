// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline built-in studio preview engine:
//  - 8-voice polyphonic electronic drum kit (Kick, Snare, Ghost, Clap/Rim,
//    choked Closed/Open Hats, resonant Perc A, metallic Ride/Crash Perc B)
//  - 8-voice strictly in-tune polyphonic warm stab synth for MIDI Slicer preview
// Pure C++20, zero heap allocations, [[clang::nonblocking]].
#pragma once

#include "cl/nonblocking.h"
#include "cl/params.h"
#include "cl/pattern.h"

#include <hba/dsp/adaa.h>
#include <hba/dsp/control.h>
#include <hba/dsp/gain.h>
#include <hba/dsp/oscillator.h>
#include <hba/dsp/pitch.h>
#include <hba/dsp/random.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cl {

class AuditionSynth {
  public:
    static constexpr int kNumVoices = 8;

    void prepare(double sampleRate) noexcept CL_NONBLOCKING {
        fs_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        cutoffCoef_ = hba::dsp::onePoleCoef(0.006, 1.0 / fs_);
        gainCoef_ = hba::dsp::onePoleCoef(0.010, 1.0 / fs_);
        qCoef_ = hba::dsp::onePoleCoef(0.010, 1.0 / fs_);
        atkStab_ = hba::dsp::onePoleCoef(0.0018, 1.0 / fs_);
        relStab_ = hba::dsp::onePoleCoef(0.028, 1.0 / fs_);
        stabEnvDecay_ = std::exp(-1.0 / (0.140 * fs_));

        // 8-voice drum kit envelope constants
        kickPitchDecay_ = std::exp(-1.0 / (0.018 * fs_));
        kickEnvDecay_ = std::exp(-1.0 / (0.135 * fs_));
        kickClickDecay_ = std::exp(-1.0 / (0.0032 * fs_));

        snarePitchDecay_ = std::exp(-1.0 / (0.012 * fs_));
        snareBodyDecay_ = std::exp(-1.0 / (0.056 * fs_));
        snareSnapDecay_ = std::exp(-1.0 / (0.078 * fs_));

        ghostDecay_ = std::exp(-1.0 / (0.032 * fs_));
        clapTailDecay_ = std::exp(-1.0 / (0.068 * fs_));
        rimDecay_ = std::exp(-1.0 / (0.024 * fs_));

        closedHatDecay_ = std::exp(-1.0 / (0.028 * fs_));
        openHatDecay_ = std::exp(-1.0 / (0.175 * fs_));

        percPitchDecay_ = std::exp(-1.0 / (0.026 * fs_));
        percDecay_ = std::exp(-1.0 / (0.068 * fs_));
        crashDecay_ = std::exp(-1.0 / (0.310 * fs_));

        snareHpCoefs_ = hba::dsp::SvfCoefs::make(1850.0, 1.15, fs_);
        ghostBpCoefs_ = hba::dsp::SvfCoefs::make(2500.0, 1.5, fs_);
        clapBpCoefs_ = hba::dsp::SvfCoefs::make(1350.0, 1.85, fs_);
        closedHatCoefs_ = hba::dsp::SvfCoefs::make(7800.0, 1.4, fs_);
        openHatCoefs_ = hba::dsp::SvfCoefs::make(6400.0, 1.25, fs_);
        percBpCoefs_ = hba::dsp::SvfCoefs::make(980.0, 2.0, fs_);
        crashHpCoefs_ = hba::dsp::SvfCoefs::make(4600.0, 1.1, fs_);
        reset();
    }

    void reset() noexcept CL_NONBLOCKING {
        for (auto& v : voices_)
            v.reset();
        kit_.reset();
        drumMode_ = true;
        slide_ = 0.56;
        drive_ = 1.35;
        pressure_ = 0.22;
        pitchScoopSt_ = 0.0;
        gainSmooth_.snap(0.0);
        ageCounter_ = 0;
    }

    void triggerDrumLane(int lane, int velocity) noexcept CL_NONBLOCKING {
        const double v = static_cast<double>(std::clamp(velocity, 1, 127)) / 127.0;
        drumMode_ = true;
        kit_.trigger(std::clamp(lane, 0, kDrumLaneCount - 1), v, fs_);
    }

    void noteOn(int note, int velocity, bool isDrum, int laneHint = -1) noexcept CL_NONBLOCKING {
        if (isDrum) {
            int lane = laneHint;
            if (lane < 0 || lane >= kDrumLaneCount) {
                switch (note) {
                case 36:
                    lane = 0;
                    break;
                case 38:
                    lane = 1;
                    break;
                case 37:
                    lane = 2;
                    break;
                case 39:
                    lane = 3;
                    break;
                case 42:
                    lane = 4;
                    break;
                case 46:
                    lane = 5;
                    break;
                case 43:
                    lane = 6;
                    break;
                default:
                    lane = 7;
                    break;
                }
            }
            triggerDrumLane(lane, velocity);
            return;
        }

        drumMode_ = false;
        const int clampedNote = std::clamp(note, 12, 120);
        const double v = static_cast<double>(std::clamp(velocity, 1, 127)) / 127.0;

        int slot = -1;
        for (int i = 0; i < kNumVoices; ++i) {
            if (voices_[static_cast<std::size_t>(i)].gate && voices_[static_cast<std::size_t>(i)].note == clampedNote) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            for (int i = 0; i < kNumVoices; ++i) {
                if (!voices_[static_cast<std::size_t>(i)].gate && voices_[static_cast<std::size_t>(i)].env < 1e-4) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot < 0) {
            slot = 0;
            for (int i = 1; i < kNumVoices; ++i) {
                if (voices_[static_cast<std::size_t>(i)].age < voices_[static_cast<std::size_t>(slot)].age)
                    slot = i;
            }
        }

        auto& voice = voices_[static_cast<std::size_t>(slot)];
        voice.note = clampedNote;
        voice.vel = v;
        voice.gate = true;
        voice.stabEnv = 1.0;
        voice.age = ++ageCounter_;
        voice.cutoffSmooth.snap(slideToCutoff(slide_));
        voice.qSmooth.snap(pressureToQ(pressure_));
    }

    void noteOff(int note) noexcept CL_NONBLOCKING {
        const int clampedNote = std::clamp(note, 12, 120);
        for (auto& v : voices_) {
            if (v.gate && v.note == clampedNote)
                v.gate = false;
        }
    }

    void allNotesOff() noexcept CL_NONBLOCKING {
        for (auto& v : voices_)
            v.gate = false;
    }

    void setBrightness(double norm01) noexcept CL_NONBLOCKING { slide_ = std::clamp(norm01, 0.0, 1.0); }
    void setDrive(double norm01) noexcept CL_NONBLOCKING { drive_ = 1.0 + 1.8 * std::clamp(norm01, 0.0, 1.0); }
    void setPressure(double norm01) noexcept CL_NONBLOCKING { pressure_ = std::clamp(norm01, 0.0, 1.0); }
    void setPitchScoop(double semitones) noexcept CL_NONBLOCKING { pitchScoopSt_ = std::clamp(semitones, -12.0, 12.0); }

    void renderSample(float gainDb, bool enabled, float& outL, float& outR) noexcept CL_NONBLOCKING {
        const double targetGain = enabled ? hba::dsp::dbToGain(static_cast<double>(gainDb)) : 0.0;
        const double g = gainSmooth_.step(targetGain, gainCoef_);

        bool anyVoice = kit_.active();
        for (const auto& v : voices_)
            anyVoice = anyVoice || v.gate || v.env > 1.0e-5;

        if (g < 1.0e-5 && !anyVoice) {
            outL = 0.0f;
            outR = 0.0f;
            return;
        }

        double sumL = 0.0;
        double sumR = 0.0;

        if (kit_.active()) {
            double kl = 0.0, kr = 0.0;
            renderKitSample(kl, kr);
            sumL += kl;
            sumR += kr;
        }

        // Clean, strictly in-tune polyphonic warm chord/bass stab preview
        constexpr std::array<double, kNumVoices> kPan{-0.22, 0.22, -0.34, 0.34, -0.14, 0.14, -0.40, 0.40};
        for (int vi = 0; vi < kNumVoices; ++vi) {
            auto& v = voices_[static_cast<std::size_t>(vi)];
            if (!v.gate && v.env < 1.0e-5)
                continue;

            v.env += ((v.gate ? 1.0 : 0.0) - v.env) * (v.gate ? atkStab_ : relStab_);
            v.stabEnv *= stabEnvDecay_;

            // Subtle ±3.5 cent warm chorus detune — ALWAYS dead-center in tune!
            const double freq = hba::dsp::midiToHz(static_cast<double>(v.note) + pitchScoopSt_);
            const double inc = std::clamp(freq / fs_, 1.0e-5, 0.44);
            const double incUp = std::clamp(inc * 1.0020, 1.0e-5, 0.44);
            const double incDn = std::clamp(inc * 0.9980, 1.0e-5, 0.44);

            v.phase += inc;
            if (v.phase >= 1.0)
                v.phase -= 1.0;
            v.detPhase += incUp;
            if (v.detPhase >= 1.0)
                v.detPhase -= 1.0;
            v.detPhase2 += incDn;
            if (v.detPhase2 >= 1.0)
                v.detPhase2 -= 1.0;

            const double sawL = hba::dsp::polyBlepSaw(v.detPhase, incUp);
            const double sawR = hba::dsp::polyBlepSaw(v.detPhase2, incDn);
            const double tri = 1.0 - 4.0 * std::abs(v.phase - 0.5);
            const double osc = 0.38 * (sawL + sawR) + 0.34 * tri;

            const double baseCut = v.cutoffSmooth.step(slideToCutoff(slide_), cutoffCoef_);
            const double envCut = std::clamp(baseCut * (0.42 + (1.1 + 1.2 * v.vel) * v.stabEnv), 160.0, 0.40 * fs_);
            const double q = v.qSmooth.step(pressureToQ(pressure_), qCoef_);
            const auto coefs = hba::dsp::SvfCoefs::make(envCut, q, fs_);
            const double body = v.filter.lowpass(coefs, v.shaper.process(osc * drive_));

            const double amp = v.env * (0.35 + 0.65 * v.stabEnv) * v.vel * 0.46;
            const double pan = kPan[static_cast<std::size_t>(vi)];
            sumL += body * amp * (1.0 - pan * 0.45);
            sumR += body * amp * (1.0 + pan * 0.45);
        }

        outL = static_cast<float>(std::tanh(sumL * g * 0.92));
        outR = static_cast<float>(std::tanh(sumR * g * 0.92));
    }

  private:
    static double slideToCutoff(double norm01) noexcept CL_NONBLOCKING {
        return 320.0 * std::pow(18.0, std::clamp(norm01, 0.0, 1.0));
    }

    static double pressureToQ(double norm01) noexcept CL_NONBLOCKING {
        return 0.85 + 2.2 * std::clamp(norm01, 0.0, 1.0);
    }

    struct DrumKit {
        double kickEnv = 0.0, kickPitch = 190.0, kickPhase = 0.0, kickClick = 0.0, kickVel = 1.0;
        double snareBodyEnv = 0.0, snareSnapEnv = 0.0, snarePitch = 240.0, snarePhase1 = 0.0, snarePhase2 = 0.0,
               snareVel = 1.0;
        double ghostEnv = 0.0, ghostPhase = 0.0, ghostVel = 1.0;
        double clapEnv = 0.0, clapVel = 1.0, rimEnv = 0.0, rimPhase = 0.0;
        int clapSample = 0;
        double chEnv = 0.0, chVel = 1.0;
        double ohEnv = 0.0, ohVel = 1.0;
        double percEnv = 0.0, percPhase = 0.0, percPitch = 300.0, percVel = 1.0;
        double crashEnv = 0.0, crashVel = 1.0;
        std::array<double, 6> hatPhases{};
        hba::dsp::SvfState snareFilter{}, ghostFilter{}, clapFilter{}, chFilter{}, ohFilter{}, percFilter{},
            crashFilter{};
        hba::dsp::Adaa1<hba::dsp::TanhShaper> kickShaper{};

        void reset() noexcept CL_NONBLOCKING {
            kickEnv = kickClick = snareBodyEnv = snareSnapEnv = ghostEnv = clapEnv = rimEnv = chEnv = ohEnv = percEnv =
                crashEnv = 0.0;
            kickPhase = snarePhase1 = snarePhase2 = ghostPhase = rimPhase = percPhase = 0.0;
            clapSample = 0;
            hatPhases.fill(0.0);
            snareFilter.reset();
            ghostFilter.reset();
            clapFilter.reset();
            chFilter.reset();
            ohFilter.reset();
            percFilter.reset();
            crashFilter.reset();
            kickShaper.reset();
        }

        bool active() const noexcept CL_NONBLOCKING {
            return kickEnv > 1e-5 || snareSnapEnv > 1e-5 || snareBodyEnv > 1e-5 || ghostEnv > 1e-5 || clapEnv > 1e-5 ||
                   rimEnv > 1e-5 || chEnv > 1e-5 || ohEnv > 1e-5 || percEnv > 1e-5 || crashEnv > 1e-5;
        }

        void trigger(int lane, double vel, double /*fs*/) noexcept CL_NONBLOCKING {
            switch (lane) {
            case 0: // Kick
                kickEnv = 1.0;
                kickClick = 1.0;
                kickPitch = 195.0;
                kickPhase = 0.0;
                kickVel = vel;
                break;
            case 1: // Snare
                snareBodyEnv = 1.0;
                snareSnapEnv = 1.0;
                snarePitch = 255.0;
                snarePhase1 = snarePhase2 = 0.0;
                snareVel = vel;
                break;
            case 2: // Ghost Snare
                ghostEnv = 1.0;
                ghostPhase = 0.0;
                ghostVel = vel;
                break;
            case 3: // Clap / Rim
                clapEnv = 1.0;
                clapSample = 0;
                clapVel = vel;
                rimEnv = 0.85;
                rimPhase = 0.0;
                break;
            case 4: // Closed Hat (chokes Open Hat!)
                chEnv = 1.0;
                chVel = vel;
                ohEnv *= 0.06;
                break;
            case 5: // Open Hat
                ohEnv = 1.0;
                ohVel = vel;
                break;
            case 6: // Perc A (resonant tom/wood)
                percEnv = 1.0;
                percPhase = 0.0;
                percPitch = 320.0;
                percVel = vel;
                break;
            default: // Perc B / Crash
                crashEnv = 1.0;
                crashVel = vel;
                break;
            }
        }
    };

    void renderKitSample(double& outL, double& outR) noexcept CL_NONBLOCKING {
        const double noise = noise_.nextBipolar();

        // 6-oscillator classic metallic TR-808/909 square cluster for hats & crash
        constexpr std::array<double, 6> kHatHz{205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
        double metal = 0.0;
        if (kit_.chEnv > 1e-5 || kit_.ohEnv > 1e-5 || kit_.crashEnv > 1e-5) {
            for (std::size_t i = 0; i < 6; ++i) {
                kit_.hatPhases[i] += kHatHz[i] / fs_;
                if (kit_.hatPhases[i] >= 1.0)
                    kit_.hatPhases[i] -= 1.0;
                metal += kit_.hatPhases[i] < 0.5 ? (1.0 / 6.0) : (-1.0 / 6.0);
            }
        }

        double l = 0.0, r = 0.0;

        // 0. Kick (sub-locked punchy drop + warm 2nd harmonic + crisp transient click)
        if (kit_.kickEnv > 1e-5) {
            kit_.kickPitch = 47.0 + (kit_.kickPitch - 47.0) * kickPitchDecay_;
            kit_.kickPhase += kit_.kickPitch / fs_;
            if (kit_.kickPhase >= 1.0)
                kit_.kickPhase -= 1.0;
            const double th = 6.283185307179586 * kit_.kickPhase;
            const double body = (std::sin(th) + 0.16 * std::sin(2.0 * th)) * kit_.kickEnv;
            const double click = noise * kit_.kickClick * 0.28;
            const double k = kit_.kickShaper.process((body * 1.18 + click) * 1.35) * kit_.kickVel * 0.96;
            l += k;
            r += k;
            kit_.kickEnv *= kickEnvDecay_;
            kit_.kickClick *= kickClickDecay_;
        }

        // 1. Snare (tight pitched dual-shell body + crisp highpassed wires)
        if (kit_.snareSnapEnv > 1e-5 || kit_.snareBodyEnv > 1e-5) {
            kit_.snarePitch = 186.0 + (kit_.snarePitch - 186.0) * snarePitchDecay_;
            kit_.snarePhase1 += kit_.snarePitch / fs_;
            if (kit_.snarePhase1 >= 1.0)
                kit_.snarePhase1 -= 1.0;
            kit_.snarePhase2 += (kit_.snarePitch * 1.72) / fs_;
            if (kit_.snarePhase2 >= 1.0)
                kit_.snarePhase2 -= 1.0;
            const double body = (0.70 * std::sin(6.283185307179586 * kit_.snarePhase1) +
                                 0.30 * std::sin(6.283185307179586 * kit_.snarePhase2)) *
                                kit_.snareBodyEnv;
            const double snap = kit_.snareFilter.highpass(snareHpCoefs_, noise) * kit_.snareSnapEnv;
            const double s = (0.54 * body + 0.72 * snap) * kit_.snareVel;
            l += s;
            r += s;
            kit_.snareBodyEnv *= snareBodyDecay_;
            kit_.snareSnapEnv *= snareSnapDecay_;
        }

        // 2. Ghost Snare (crisp woody tap, slightly left)
        if (kit_.ghostEnv > 1e-5) {
            kit_.ghostPhase += 262.0 / fs_;
            if (kit_.ghostPhase >= 1.0)
                kit_.ghostPhase -= 1.0;
            const double gBody = std::sin(6.283185307179586 * kit_.ghostPhase) * 0.32;
            const double gSnap = kit_.ghostFilter.bandpass(ghostBpCoefs_, noise) * 0.78;
            const double gSig = (gBody + gSnap) * kit_.ghostEnv * kit_.ghostVel * 0.78;
            l += gSig * 1.10;
            r += gSig * 0.90;
            kit_.ghostEnv *= ghostDecay_;
        }

        // 3. Clap / Rim (3-tap micro-flam filtered clap burst + woody rim crack)
        if (kit_.clapEnv > 1e-5 || kit_.rimEnv > 1e-5) {
            const double ms = static_cast<double>(kit_.clapSample++) * (1000.0 / fs_);
            double burst = 1.0;
            if (ms < 10.0)
                burst = 0.85;
            else if (ms < 19.0)
                burst = 0.95;
            else if (ms < 28.0)
                burst = 1.10;
            else
                kit_.clapEnv *= clapTailDecay_;
            const double cNoise = kit_.clapFilter.bandpass(clapBpCoefs_, noise) * kit_.clapEnv * burst * 0.76;
            kit_.rimPhase += 840.0 / fs_;
            if (kit_.rimPhase >= 1.0)
                kit_.rimPhase -= 1.0;
            const double rim = std::sin(6.283185307179586 * kit_.rimPhase) * kit_.rimEnv * 0.24;
            const double sig = (cNoise + rim) * kit_.clapVel;
            l += sig * 0.92;
            r += sig * 1.08;
            kit_.rimEnv *= rimDecay_;
        }

        // 4. Closed Hat (crisp right-shifted metallic tick)
        if (kit_.chEnv > 1e-5) {
            const double h =
                kit_.chFilter.highpass(closedHatCoefs_, 0.52 * noise + 0.48 * metal) * kit_.chEnv * kit_.chVel * 0.60;
            l += h * 0.86;
            r += h * 1.14;
            kit_.chEnv *= closedHatDecay_;
        }

        // 5. Open Hat (wide shimmer, choked by CH)
        if (kit_.ohEnv > 1e-5) {
            const double oh =
                kit_.ohFilter.highpass(openHatCoefs_, 0.46 * noise + 0.54 * metal) * kit_.ohEnv * kit_.ohVel * 0.58;
            l += oh * 0.82;
            r += oh * 1.18;
            kit_.ohEnv *= openHatDecay_;
        }

        // 6. Perc A (pitched resonant tom/bongo, left)
        if (kit_.percEnv > 1e-5) {
            kit_.percPitch = 148.0 + (kit_.percPitch - 148.0) * percPitchDecay_;
            kit_.percPhase += kit_.percPitch / fs_;
            if (kit_.percPhase >= 1.0)
                kit_.percPhase -= 1.0;
            const double pTone = std::sin(6.283185307179586 * kit_.percPhase);
            const double pClick = kit_.percFilter.bandpass(percBpCoefs_, noise) * 0.42;
            const double pSig = (0.72 * pTone + pClick) * kit_.percEnv * kit_.percVel * 0.78;
            l += pSig * 1.16;
            r += pSig * 0.84;
            kit_.percEnv *= percDecay_;
        }

        // 7. Perc B / Crash (airy metallic ride/crash shimmer, right)
        if (kit_.crashEnv > 1e-5) {
            const double cr = kit_.crashFilter.highpass(crashHpCoefs_, 0.56 * noise + 0.44 * metal) * kit_.crashEnv *
                              kit_.crashVel * 0.48;
            l += cr * 0.78;
            r += cr * 1.22;
            kit_.crashEnv *= crashDecay_;
        }

        outL = l;
        outR = r;
    }

    struct Voice {
        double phase = 0.0;
        double detPhase = 0.0;
        double detPhase2 = 0.0;
        double env = 0.0;
        double stabEnv = 0.0;
        double vel = 0.8;
        int note = 60;
        bool gate = false;
        std::uint64_t age = 0;
        hba::dsp::Smoother cutoffSmooth{};
        hba::dsp::Smoother qSmooth{};
        hba::dsp::SvfState filter{};
        hba::dsp::Adaa1<hba::dsp::TanhShaper> shaper{};

        void reset() noexcept CL_NONBLOCKING {
            phase = detPhase = detPhase2 = 0.0;
            env = stabEnv = 0.0;
            gate = false;
            age = 0;
            cutoffSmooth.snap(1400.0);
            qSmooth.snap(1.1);
            filter.reset();
            shaper.reset();
        }
    };

    double fs_ = 48000.0;
    double cutoffCoef_ = 0.004;
    double gainCoef_ = 0.002;
    double qCoef_ = 0.003;
    double atkStab_ = 0.012;
    double relStab_ = 0.0008;
    double stabEnvDecay_ = 0.9998;

    double kickPitchDecay_ = 0.999;
    double kickEnvDecay_ = 0.9998;
    double kickClickDecay_ = 0.993;
    double snarePitchDecay_ = 0.998;
    double snareBodyDecay_ = 0.9995;
    double snareSnapDecay_ = 0.9996;
    double ghostDecay_ = 0.9992;
    double clapTailDecay_ = 0.9996;
    double rimDecay_ = 0.9990;
    double closedHatDecay_ = 0.9992;
    double openHatDecay_ = 0.9998;
    double percPitchDecay_ = 0.9991;
    double percDecay_ = 0.9996;
    double crashDecay_ = 0.9999;

    double slide_ = 0.56;
    double drive_ = 1.35;
    double pressure_ = 0.22;
    double pitchScoopSt_ = 0.0;

    std::array<Voice, kNumVoices> voices_{};
    hba::dsp::Smoother gainSmooth_{};
    DrumKit kit_{};
    bool drumMode_ = true;
    std::uint64_t ageCounter_ = 0;
    hba::dsp::Lcg32 noise_{0x12345678u};

    hba::dsp::SvfCoefs snareHpCoefs_{};
    hba::dsp::SvfCoefs ghostBpCoefs_{};
    hba::dsp::SvfCoefs clapBpCoefs_{};
    hba::dsp::SvfCoefs closedHatCoefs_{};
    hba::dsp::SvfCoefs openHatCoefs_{};
    hba::dsp::SvfCoefs percBpCoefs_{};
    hba::dsp::SvfCoefs crashHpCoefs_{};
};

} // namespace cl
