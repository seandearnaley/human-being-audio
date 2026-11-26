// SPDX-License-Identifier: AGPL-3.0-or-later
// Studio-grade 8-voice PolyBLEP audition synth + 6-voice polyphonic breakbeat
// drum kit for VST3/AU/Standalone preview. Supports per-channel MPE Pitch Bend,
// CC74 Slide (brightness), and Channel Pressure (resonance).
// Pure C++20, zero heap allocations, [[clang::nonblocking]].
#pragma once

#include "cl/nonblocking.h"
#include "cl/params.h"

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
        cutoffCoef_ = hba::dsp::onePoleCoef(0.008, 1.0 / fs_);
        gainCoef_ = hba::dsp::onePoleCoef(0.012, 1.0 / fs_);
        bendCoef_ = hba::dsp::onePoleCoef(0.006, 1.0 / fs_);
        qCoef_ = hba::dsp::onePoleCoef(0.010, 1.0 / fs_);
        atkPluck_ = hba::dsp::onePoleCoef(0.0012, 1.0 / fs_);
        relPluck_ = hba::dsp::onePoleCoef(0.032, 1.0 / fs_);
        atkStd_ = hba::dsp::onePoleCoef(0.0035, 1.0 / fs_);
        relStd_ = hba::dsp::onePoleCoef(0.022, 1.0 / fs_);
        pluckDecay_ = std::exp(-1.0 / (0.088 * fs_));
        subPunchDecay_ = std::exp(-1.0 / (0.022 * fs_));

        // 6-voice polyphonic drum kit decay constants & filters.
        kickPitchDecay_ = std::exp(-1.0 / (0.020 * fs_));
        kickEnvDecay_ = std::exp(-1.0 / (0.125 * fs_));
        kickClickDecay_ = std::exp(-1.0 / (0.004 * fs_));
        snareBodyDecay_ = std::exp(-1.0 / (0.055 * fs_));
        snareSnapDecay_ = std::exp(-1.0 / (0.072 * fs_));
        ghostDecay_ = std::exp(-1.0 / (0.036 * fs_));
        closedHatDecay_ = std::exp(-1.0 / (0.030 * fs_));
        openHatDecay_ = std::exp(-1.0 / (0.145 * fs_));
        percDecay_ = std::exp(-1.0 / (0.042 * fs_));

        snareHpCoefs_ = hba::dsp::SvfCoefs::make(1900.0, 1.25, fs_);
        ghostBpCoefs_ = hba::dsp::SvfCoefs::make(2400.0, 1.6, fs_);
        closedHatCoefs_ = hba::dsp::SvfCoefs::make(7400.0, 1.5, fs_);
        openHatCoefs_ = hba::dsp::SvfCoefs::make(6200.0, 1.3, fs_);
        percBpCoefs_ = hba::dsp::SvfCoefs::make(1250.0, 2.2, fs_);
        reset();
    }

    void reset() noexcept CL_NONBLOCKING {
        for (auto& v : voices_)
            v.reset();
        kit_.reset();
        drumMode_ = false;
        drive_ = 1.8;
        channelBend_.fill(0.0);
        channelSlide_.fill(0.45);
        channelPressure_.fill(0.20);
        gainSmooth_.snap(0.0);
        ageCounter_ = 0;
    }

    void noteOn(int channel, int note, int velocity, bool useKit, bool isDrumMode = false) noexcept CL_NONBLOCKING {
        const double v = static_cast<double>(std::clamp(velocity, 1, 127)) / 127.0;
        if (useKit && !drumMode_)
            allNotesOff();
        drumMode_ = useKit;
        if (useKit) {
            int row = 0;
            if (isDrumMode) {
                if (note == 36)
                    row = 0; // Kick
                else if (note == 38)
                    row = 1; // Snare
                else if (note == 39)
                    row = 2; // Ghost Snare
                else if (note == 42)
                    row = 3; // Closed Hat
                else if (note == 46)
                    row = 4; // Open Hat
                else
                    row = 5; // Ratchet Perc
            } else {
                // Allow Tone: Kit in Mono/MPE modes to articulate melodic pitches across the 6 drum voices.
                if (note < 40)
                    row = 0;
                else if (note < 48)
                    row = 1;
                else if (note < 54)
                    row = 2;
                else if (note < 62)
                    row = 3;
                else if (note < 70)
                    row = 4;
                else
                    row = 5;
            }
            kit_.trigger(row, v);
            return;
        }

        const int ch = std::clamp(channel, 1, 16);
        const int clampedNote = std::clamp(note, 12, 120);

        // Reuse matching (channel, note), else free voice, else oldest voice.
        int slot = -1;
        for (int i = 0; i < kNumVoices; ++i) {
            if (voices_[static_cast<std::size_t>(i)].gate && voices_[static_cast<std::size_t>(i)].note == clampedNote &&
                voices_[static_cast<std::size_t>(i)].channel == ch) {
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

        const auto chIdx = static_cast<std::size_t>(ch - 1);
        auto& voice = voices_[static_cast<std::size_t>(slot)];
        voice.note = clampedNote;
        voice.channel = ch;
        voice.vel = v;
        voice.gate = true;
        voice.pluckEnv = 1.0;
        voice.punchEnv = 1.0;
        voice.age = ++ageCounter_;
        voice.bendSmooth.snap(channelBend_[chIdx]);
        voice.cutoffSmooth.snap(slideToCutoff(channelSlide_[chIdx]));
        voice.qSmooth.snap(pressureToQ(channelPressure_[chIdx]));
    }

    void noteOff(int channel, int note) noexcept CL_NONBLOCKING {
        const int ch = std::clamp(channel, 1, 16);
        const int clampedNote = std::clamp(note, 12, 120);
        for (auto& v : voices_) {
            if (v.gate && v.note == clampedNote && v.channel == ch)
                v.gate = false;
        }
    }

    void allNotesOff() noexcept CL_NONBLOCKING {
        for (auto& v : voices_)
            v.gate = false;
    }

    void setBendSemitones(int channel, double semitones) noexcept CL_NONBLOCKING {
        const auto idx = static_cast<std::size_t>(std::clamp(channel, 1, 16) - 1);
        channelBend_[idx] = std::clamp(semitones, -48.0, 48.0);
    }

    // Channel 1 updates master default (and Ch 1); MPE member channels 2..16 update per-channel brightness.
    void setBrightness(int channel, double norm01) noexcept CL_NONBLOCKING {
        const double clamped = std::clamp(norm01, 0.0, 1.0);
        const int ch = std::clamp(channel, 1, 16);
        if (ch == 1)
            channelSlide_.fill(clamped);
        else
            channelSlide_[static_cast<std::size_t>(ch - 1)] = clamped;
    }

    void setBrightness(double norm01) noexcept CL_NONBLOCKING { setBrightness(1, norm01); }

    void setDrive(double norm01) noexcept CL_NONBLOCKING { drive_ = 1.0 + 3.4 * std::clamp(norm01, 0.0, 1.0); }

    void setPressure(int channel, double norm01) noexcept CL_NONBLOCKING {
        const double clamped = std::clamp(norm01, 0.0, 1.0);
        const int ch = std::clamp(channel, 1, 16);
        if (ch == 1)
            channelPressure_.fill(clamped);
        else
            channelPressure_[static_cast<std::size_t>(ch - 1)] = clamped;
    }

    void setPressure(double norm01) noexcept CL_NONBLOCKING { setPressure(1, norm01); }

    void renderSample(float gainDb, bool enabled, AuditionTone tone, float& outL, float& outR) noexcept CL_NONBLOCKING {
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

        const bool useKit = drumMode_ || (tone == AuditionTone::kit);
        if (useKit) {
            if (tone == AuditionTone::kit)
                allNotesOff();
            double kl = 0.0, kr = 0.0;
            renderKitSample(kl, kr);
            outL = static_cast<float>(std::tanh(kl * g * 0.90));
            outR = static_cast<float>(std::tanh(kr * g * 0.90));
            return;
        }

        const double atk = tone == AuditionTone::pluck ? atkPluck_ : atkStd_;
        const double rel = tone == AuditionTone::pluck ? relPluck_ : relStd_;

        double sumL = 0.0;
        double sumR = 0.0;

        constexpr std::array<double, kNumVoices> kPan{-0.24, 0.24, -0.38, 0.38, -0.18, 0.18, -0.44, 0.44};

        for (int vi = 0; vi < kNumVoices; ++vi) {
            auto& v = voices_[static_cast<std::size_t>(vi)];
            if (!v.gate && v.env < 1.0e-5)
                continue;

            const auto chIdx = static_cast<std::size_t>(v.channel - 1);
            v.env += ((v.gate ? 1.0 : 0.0) - v.env) * (v.gate ? atk : rel);
            v.pluckEnv *= pluckDecay_;
            v.punchEnv *= subPunchDecay_;

            const double bend = v.bendSmooth.step(channelBend_[chIdx], bendCoef_);
            const double subPunchSt = (tone == AuditionTone::sub) ? (9.0 * v.punchEnv) : 0.0;
            const double freq = hba::dsp::midiToHz(static_cast<double>(v.note) + bend + subPunchSt);
            const double inc = std::clamp(freq / fs_, 1.0e-5, 0.45);
            const double detUp = std::clamp(inc * 1.0064, 1.0e-5, 0.45);   // +11 cents
            const double detDown = std::clamp(inc * 0.9936, 1.0e-5, 0.45); // -11 cents

            v.phase += inc;
            if (v.phase >= 1.0)
                v.phase -= 1.0;
            v.detPhase += detUp;
            if (v.detPhase >= 1.0)
                v.detPhase -= 1.0;
            v.detPhase2 += detDown;
            if (v.detPhase2 >= 1.0)
                v.detPhase2 -= 1.0;
            v.subPhase += inc * 0.5;
            if (v.subPhase >= 1.0)
                v.subPhase -= 1.0;

            const double cutoff = v.cutoffSmooth.step(slideToCutoff(channelSlide_[chIdx]), cutoffCoef_);
            const double q = v.qSmooth.step(pressureToQ(channelPressure_[chIdx]), qCoef_);

            double voiceL = 0.0;
            double voiceR = 0.0;

            if (tone == AuditionTone::sub) {
                // Phase-locked pitch-punched sub sine with warm 2nd harmonic.
                const double p = 6.283185307179586 * v.phase;
                const double fund = std::sin(p);
                const double warm = 0.18 * std::sin(2.0 * p);
                const double s = (fund + warm) * v.env * v.vel * 0.88;
                voiceL = s;
                voiceR = s;
            } else if (tone == AuditionTone::pluck) {
                // PolyBLEP saw + pulse blend through fast exponential pluck SVF.
                const double saw = hba::dsp::polyBlepSaw(v.phase, inc);
                const double sq = hba::dsp::polyBlepSquare(v.phase, inc);
                const double osc = 0.72 * saw + 0.28 * sq;
                const double plCutoff =
                    std::clamp(cutoff * (0.38 + (1.4 + 0.9 * v.vel) * v.pluckEnv), 120.0, 0.42 * fs_);
                const auto coefs = hba::dsp::SvfCoefs::make(plCutoff, q * 0.85, fs_);
                const double body = v.filter.lowpass(coefs, v.shaper.process(osc * (1.1 + 0.35 * drive_)));
                const double amp = v.env * (0.24 + 0.76 * v.pluckEnv) * v.vel;
                const double pan = kPan[static_cast<std::size_t>(vi)];
                voiceL = body * amp * (1.0 - pan * 0.45);
                voiceR = body * amp * (1.0 + pan * 0.45);
            } else {
                // Reese: 3-osc unison PolyBLEP saws (L/C/R detuned +-11 ct) + ADAA1 Tanh + stereo SVF + mono sub.
                const double sC = hba::dsp::polyBlepSaw(v.phase, inc);
                const double sL = hba::dsp::polyBlepSaw(v.detPhase, detUp);
                const double sR = hba::dsp::polyBlepSaw(v.detPhase2, detDown);
                const double midOsc = 0.42 * sC + 0.29 * (sL + sR);
                const double sideOsc = 0.55 * (sL - sR);
                const double sub = std::sin(6.283185307179586 * v.subPhase);
                const auto reeseCoefs = hba::dsp::SvfCoefs::make(std::min(cutoff, 0.42 * fs_), q, fs_);
                const double growl = v.filter.lowpass(reeseCoefs, v.shaper.process(midOsc * drive_));
                const double side = v.sideFilter.lowpass(reeseCoefs, sideOsc * 0.48);
                const double amp = v.env * v.vel;
                voiceL = (0.44 * sub + 0.56 * (growl + side)) * amp;
                voiceR = (0.44 * sub + 0.56 * (growl - side)) * amp;
            }

            sumL += voiceL * 0.56;
            sumR += voiceR * 0.56;
        }

        outL = static_cast<float>(std::tanh(sumL * g * 0.86));
        outR = static_cast<float>(std::tanh(sumR * g * 0.86));
    }

  private:
    static double slideToCutoff(double norm01) noexcept CL_NONBLOCKING {
        return 170.0 * std::pow(26.0, std::clamp(norm01, 0.0, 1.0));
    }

    static double pressureToQ(double norm01) noexcept CL_NONBLOCKING {
        return 1.3 + 4.4 * std::clamp(norm01, 0.0, 1.0);
    }

    struct DrumKit {
        // 6 independent polyphonic drum voices: Kick, Snare, Ghost, Closed Hat, Open Hat, Ratchet Perc
        double kickEnv = 0.0, kickPitch = 180.0, kickPhase = 0.0, kickClick = 0.0, kickVel = 1.0;
        double snareBodyEnv = 0.0, snareSnapEnv = 0.0, snarePhase1 = 0.0, snarePhase2 = 0.0, snareVel = 1.0;
        double ghostEnv = 0.0, ghostPhase = 0.0, ghostVel = 1.0;
        double chEnv = 0.0, chVel = 1.0;
        double ohEnv = 0.0, ohVel = 1.0;
        double percEnv = 0.0, percPhase = 0.0, percPitch = 620.0, percVel = 1.0;
        std::array<double, 4> hatPhases{};
        hba::dsp::SvfState snareFilter{}, ghostFilter{}, chFilter{}, ohFilter{}, percFilter{};

        void reset() noexcept CL_NONBLOCKING {
            kickEnv = kickClick = snareBodyEnv = snareSnapEnv = ghostEnv = chEnv = ohEnv = percEnv = 0.0;
            kickPhase = snarePhase1 = snarePhase2 = ghostPhase = percPhase = 0.0;
            hatPhases.fill(0.0);
            snareFilter.reset();
            ghostFilter.reset();
            chFilter.reset();
            ohFilter.reset();
            percFilter.reset();
        }

        bool active() const noexcept CL_NONBLOCKING {
            return kickEnv > 1e-5 || snareSnapEnv > 1e-5 || snareBodyEnv > 1e-5 || ghostEnv > 1e-5 || chEnv > 1e-5 ||
                   ohEnv > 1e-5 || percEnv > 1e-5;
        }

        void trigger(int row, double vel) noexcept CL_NONBLOCKING {
            switch (row) {
            case 0: // Kick
                kickEnv = 1.0;
                kickClick = 1.0;
                kickPitch = 185.0;
                kickPhase = 0.0;
                kickVel = vel;
                break;
            case 1: // Snare
                snareBodyEnv = 1.0;
                snareSnapEnv = 1.0;
                snarePhase1 = snarePhase2 = 0.0;
                snareVel = vel;
                break;
            case 2: // Ghost Snare
                ghostEnv = 1.0;
                ghostPhase = 0.0;
                ghostVel = vel;
                break;
            case 3: // Closed Hat (chokes Open Hat like a real kit!)
                chEnv = 1.0;
                chVel = vel;
                ohEnv *= 0.08;
                break;
            case 4: // Open Hat
                ohEnv = 1.0;
                ohVel = vel;
                break;
            default: // Ratchet Perc
                percEnv = 1.0;
                percPhase = 0.0;
                percPitch = 680.0;
                percVel = vel;
                break;
            }
        }
    };

    void renderKitSample(double& outL, double& outR) noexcept CL_NONBLOCKING {
        const double noise = noise_.nextBipolar();

        // Shared inharmonic TR-808 metallic hat ring (4 square oscillators)
        constexpr std::array<double, 4> kHatHz{296.0, 415.0, 543.0, 800.0};
        double metal = 0.0;
        if (kit_.chEnv > 1e-5 || kit_.ohEnv > 1e-5) {
            for (std::size_t i = 0; i < 4; ++i) {
                kit_.hatPhases[i] += kHatHz[i] / fs_;
                if (kit_.hatPhases[i] >= 1.0)
                    kit_.hatPhases[i] -= 1.0;
                metal += kit_.hatPhases[i] < 0.5 ? 0.25 : -0.25;
            }
        }

        double l = 0.0, r = 0.0;

        // 0. Kick (mono punch)
        if (kit_.kickEnv > 1e-5) {
            kit_.kickPitch = 46.0 + (kit_.kickPitch - 46.0) * kickPitchDecay_;
            kit_.kickPhase += kit_.kickPitch / fs_;
            if (kit_.kickPhase >= 1.0)
                kit_.kickPhase -= 1.0;
            const double body = std::sin(6.283185307179586 * kit_.kickPhase) * kit_.kickEnv;
            const double click = noise * kit_.kickClick * 0.24;
            const double k = (body * 1.15 + click) * kit_.kickVel;
            l += k;
            r += k;
            kit_.kickEnv *= kickEnvDecay_;
            kit_.kickClick *= kickClickDecay_;
        }

        // 1. Snare (center tonal dual-body + crisp stereo wire snap)
        if (kit_.snareSnapEnv > 1e-5 || kit_.snareBodyEnv > 1e-5) {
            kit_.snarePhase1 += 192.0 / fs_;
            if (kit_.snarePhase1 >= 1.0)
                kit_.snarePhase1 -= 1.0;
            kit_.snarePhase2 += 331.0 / fs_;
            if (kit_.snarePhase2 >= 1.0)
                kit_.snarePhase2 -= 1.0;
            const double body = (0.65 * std::sin(6.283185307179586 * kit_.snarePhase1) +
                                 0.35 * std::sin(6.283185307179586 * kit_.snarePhase2)) *
                                kit_.snareBodyEnv;
            const double snap = kit_.snareFilter.highpass(snareHpCoefs_, noise) * kit_.snareSnapEnv;
            const double s = (0.48 * body + 0.68 * snap) * kit_.snareVel;
            l += s;
            r += s;
            kit_.snareBodyEnv *= snareBodyDecay_;
            kit_.snareSnapEnv *= snareSnapDecay_;
        }

        // 2. Ghost Snare (slightly left-panned crisp woody tap)
        if (kit_.ghostEnv > 1e-5) {
            kit_.ghostPhase += 265.0 / fs_;
            if (kit_.ghostPhase >= 1.0)
                kit_.ghostPhase -= 1.0;
            const double gBody = std::sin(6.283185307179586 * kit_.ghostPhase) * 0.35;
            const double gSnap = kit_.ghostFilter.bandpass(ghostBpCoefs_, noise) * 0.75;
            const double gSig = (gBody + gSnap) * kit_.ghostEnv * kit_.ghostVel * 0.78;
            l += gSig * 1.08;
            r += gSig * 0.92;
            kit_.ghostEnv *= ghostDecay_;
        }

        // 3. Closed Hat (slightly right-panned crisp metallic sizzle)
        if (kit_.chEnv > 1e-5) {
            const double h =
                kit_.chFilter.highpass(closedHatCoefs_, 0.55 * noise + 0.45 * metal) * kit_.chEnv * kit_.chVel * 0.65;
            l += h * 0.88;
            r += h * 1.12;
            kit_.chEnv *= closedHatDecay_;
        }

        // 4. Open Hat (wide right shimmer)
        if (kit_.ohEnv > 1e-5) {
            const double oh =
                kit_.ohFilter.highpass(openHatCoefs_, 0.50 * noise + 0.50 * metal) * kit_.ohEnv * kit_.ohVel * 0.62;
            l += oh * 0.84;
            r += oh * 1.16;
            kit_.ohEnv *= openHatDecay_;
        }

        // 5. Ratchet Perc (left-panned woody rim/bongo transient)
        if (kit_.percEnv > 1e-5) {
            kit_.percPitch = 340.0 + (kit_.percPitch - 340.0) * kickPitchDecay_;
            kit_.percPhase += kit_.percPitch / fs_;
            if (kit_.percPhase >= 1.0)
                kit_.percPhase -= 1.0;
            const double pTone = std::sin(6.283185307179586 * kit_.percPhase);
            const double pClick = kit_.percFilter.bandpass(percBpCoefs_, noise) * 0.45;
            const double pSig = (0.68 * pTone + pClick) * kit_.percEnv * kit_.percVel * 0.82;
            l += pSig * 1.15;
            r += pSig * 0.85;
            kit_.percEnv *= percDecay_;
        }

        outL = l;
        outR = r;
    }

    struct Voice {
        double phase = 0.0;
        double detPhase = 0.0;
        double detPhase2 = 0.0;
        double subPhase = 0.0;
        double env = 0.0;
        double pluckEnv = 0.0;
        double punchEnv = 0.0;
        double vel = 0.8;
        int note = 41;
        int channel = 1;
        bool gate = false;
        std::uint64_t age = 0;
        hba::dsp::Smoother bendSmooth{};
        hba::dsp::Smoother cutoffSmooth{};
        hba::dsp::Smoother qSmooth{};
        hba::dsp::SvfState filter{};
        hba::dsp::SvfState sideFilter{};
        hba::dsp::Adaa1<hba::dsp::TanhShaper> shaper{};

        void reset() noexcept CL_NONBLOCKING {
            phase = detPhase = detPhase2 = subPhase = 0.0;
            env = pluckEnv = punchEnv = 0.0;
            gate = false;
            age = 0;
            bendSmooth.snap(0.0);
            cutoffSmooth.snap(900.0);
            qSmooth.snap(1.8);
            filter.reset();
            sideFilter.reset();
            shaper.reset();
        }
    };

    double fs_ = 48000.0;
    double cutoffCoef_ = 0.003;
    double gainCoef_ = 0.002;
    double bendCoef_ = 0.004;
    double qCoef_ = 0.003;
    double atkPluck_ = 0.014;
    double relPluck_ = 0.0006;
    double atkStd_ = 0.005;
    double relStd_ = 0.0009;
    double pluckDecay_ = 0.9998;
    double subPunchDecay_ = 0.999;

    double kickPitchDecay_ = 0.999;
    double kickEnvDecay_ = 0.9998;
    double kickClickDecay_ = 0.994;
    double snareBodyDecay_ = 0.9995;
    double snareSnapDecay_ = 0.9996;
    double ghostDecay_ = 0.9993;
    double closedHatDecay_ = 0.9992;
    double openHatDecay_ = 0.9998;
    double percDecay_ = 0.9994;

    double drive_ = 1.8;

    std::array<Voice, kNumVoices> voices_{};
    std::array<double, 16> channelBend_{};
    std::array<double, 16> channelSlide_{};
    std::array<double, 16> channelPressure_{};
    hba::dsp::Smoother gainSmooth_{};
    DrumKit kit_{};
    bool drumMode_ = false;
    std::uint64_t ageCounter_ = 0;
    hba::dsp::Lcg32 noise_{0x12345678u};
    hba::dsp::SvfCoefs snareHpCoefs_{};
    hba::dsp::SvfCoefs ghostBpCoefs_{};
    hba::dsp::SvfCoefs closedHatCoefs_{};
    hba::dsp::SvfCoefs openHatCoefs_{};
    hba::dsp::SvfCoefs percBpCoefs_{};
};

} // namespace cl
