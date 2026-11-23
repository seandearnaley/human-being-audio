// SPDX-License-Identifier: AGPL-3.0-or-later
// Lightweight 4-voice stereo poly-aware audition synth for VST3/Standalone
// preview. Reuses hba::dsp primitives (SVF, ADAA1 Tanh, Gain, Smoother).
// Pure C++20, zero heap allocations, [[clang::nonblocking]].
#pragma once

#include "cl/nonblocking.h"
#include "cl/params.h"

#include <hba/dsp/adaa.h>
#include <hba/dsp/control.h>
#include <hba/dsp/gain.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cl {

class AuditionSynth {
  public:
    static constexpr int kNumVoices = 4;

    void prepare(double sampleRate) noexcept CL_NONBLOCKING {
        fs_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        cutoffCoef_ = hba::dsp::onePoleCoef(0.008, 1.0 / fs_);
        gainCoef_ = hba::dsp::onePoleCoef(0.012, 1.0 / fs_);
        bendCoef_ = hba::dsp::onePoleCoef(0.006, 1.0 / fs_);
        qCoef_ = hba::dsp::onePoleCoef(0.010, 1.0 / fs_);
        atkPluck_ = hba::dsp::onePoleCoef(0.0015, 1.0 / fs_);
        relPluck_ = hba::dsp::onePoleCoef(0.035, 1.0 / fs_);
        atkStd_ = hba::dsp::onePoleCoef(0.004, 1.0 / fs_);
        relStd_ = hba::dsp::onePoleCoef(0.024, 1.0 / fs_);
        pluckDecay_ = std::exp(-1.0 / (0.095 * fs_));
        kickPitchDecay_ = std::exp(-1.0 / (0.025 * fs_));
        kickEnvDecay_ = std::exp(-1.0 / (0.11 * fs_));
        openHatDecay_ = std::exp(-1.0 / (0.09 * fs_));
        shortDrumDecay_ = std::exp(-1.0 / (0.045 * fs_));
        hatCoefs_ = hba::dsp::SvfCoefs::make(6800.0, 1.4, fs_);
        snareCoefs_ = hba::dsp::SvfCoefs::make(1800.0, 1.4, fs_);
        reset();
    }

    void reset() noexcept CL_NONBLOCKING {
        for (auto& v : voices_)
            v.reset();
        drumEnv_ = 0.0;
        drumPitch_ = 160.0;
        drumPhase_ = 0.0;
        drumMode_ = false;
        drumIsKick_ = true;
        drumIsHat_ = false;
        cutoffTarget_ = 900.0;
        qTarget_ = 1.8;
        drive_ = 1.8;
        channelBend_.fill(0.0);
        drumFilter_.reset();
        cutoffSmooth_.snap(900.0);
        gainSmooth_.snap(0.0);
        qSmooth_.snap(1.8);
        ageCounter_ = 0;
    }

    void noteOn(int channel, int note, int velocity, bool useKit, bool isDrumMode = false) noexcept CL_NONBLOCKING {
        const double v = static_cast<double>(std::clamp(velocity, 1, 127)) / 127.0;
        drumMode_ = useKit;
        if (useKit) {
            drumVel_ = v;
            drumEnv_ = 1.0;
            drumPhase_ = 0.0;
            if (isDrumMode) {
                drumIsKick_ = (note == 36);
                drumIsHat_ = (note == 42 || note == 46);
                drumPitch_ = drumIsKick_ ? 165.0 : (note == 38 ? 205.0 : 320.0);
                drumDecay_ = drumIsKick_ ? kickEnvDecay_ : (note == 46 ? openHatDecay_ : shortDrumDecay_);
            } else {
                // Allow Tone: Kit in Mono/MPE modes to articulate melodic pitches as Kick/Snare/Hat/Perc.
                drumIsKick_ = (note < 46);
                drumIsHat_ = (note >= 60);
                drumPitch_ = drumIsKick_ ? 165.0 : (drumIsHat_ ? 340.0 : 210.0);
                drumDecay_ = drumIsKick_ ? kickEnvDecay_ : shortDrumDecay_;
            }
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

        auto& voice = voices_[static_cast<std::size_t>(slot)];
        voice.note = clampedNote;
        voice.channel = ch;
        voice.vel = v;
        voice.gate = true;
        voice.pluckEnv = 1.0;
        voice.age = ++ageCounter_;
        voice.bendSmooth.snap(channelBend_[static_cast<std::size_t>(ch - 1)]);
    }

    void noteOff(int channel, int note) noexcept CL_NONBLOCKING {
        if (drumMode_)
            return;
        const int ch = std::clamp(channel, 1, 16);
        for (auto& v : voices_) {
            if (v.gate && v.note == note && v.channel == ch)
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

    void setBrightness(double norm01) noexcept CL_NONBLOCKING {
        const double clamped = std::clamp(norm01, 0.0, 1.0);
        cutoffTarget_ = 180.0 * std::pow(22.0, clamped);
    }

    void setDrive(double norm01) noexcept CL_NONBLOCKING { drive_ = 1.0 + 3.4 * std::clamp(norm01, 0.0, 1.0); }

    void setPressure(double norm01) noexcept CL_NONBLOCKING { qTarget_ = 1.4 + 4.2 * std::clamp(norm01, 0.0, 1.0); }

    void renderSample(float gainDb, bool enabled, AuditionTone tone, float& outL, float& outR) noexcept CL_NONBLOCKING {
        const double targetGain = enabled ? hba::dsp::dbToGain(static_cast<double>(gainDb)) : 0.0;
        const double g = gainSmooth_.step(targetGain, gainCoef_);

        bool anyVoice = drumEnv_ > 1.0e-5;
        for (const auto& v : voices_)
            anyVoice = anyVoice || v.gate || v.env > 1.0e-5;

        if (g < 1.0e-5 && !anyVoice) {
            outL = 0.0f;
            outR = 0.0f;
            return;
        }

        const bool useKit = drumMode_ || (tone == AuditionTone::kit);
        if (useKit) {
            double sample = 0.0;
            if (drumEnv_ > 1.0e-5) {
                noiseState_ = noiseState_ * 1664525U + 1013904223U;
                const double noise = (static_cast<double>(noiseState_) / 2147483648.0) - 1.0;
                if (drumIsKick_) {
                    drumPitch_ = 46.0 + (drumPitch_ - 46.0) * kickPitchDecay_;
                    drumPhase_ += drumPitch_ / fs_;
                    if (drumPhase_ >= 1.0)
                        drumPhase_ -= 1.0;
                    sample = std::sin(6.283185307179586 * drumPhase_) * drumEnv_ * drumVel_ * 1.1;
                } else if (drumIsHat_) {
                    const double f = drumFilter_.highpass(hatCoefs_, noise);
                    sample = f * drumEnv_ * drumVel_ * 0.85;
                } else {
                    // Snare / perc: tonal pitch body blended with bandpass noise snap.
                    drumPhase_ += drumPitch_ / fs_;
                    if (drumPhase_ >= 1.0)
                        drumPhase_ -= 1.0;
                    const double body = std::sin(6.283185307179586 * drumPhase_) * drumEnv_ * drumEnv_;
                    const double snap = drumFilter_.bandpass(snareCoefs_, noise);
                    sample = (0.45 * body + 0.65 * snap) * drumEnv_ * drumVel_;
                }
                drumEnv_ *= drumDecay_;
            }
            const float y = static_cast<float>(std::tanh(sample * g * 0.85));
            outL = y;
            outR = y;
            return;
        }

        const double cutoff = cutoffSmooth_.step(cutoffTarget_, cutoffCoef_);
        const double q = qSmooth_.step(qTarget_, qCoef_);
        const double atk = tone == AuditionTone::pluck ? atkPluck_ : atkStd_;
        const double rel = tone == AuditionTone::pluck ? relPluck_ : relStd_;
        const auto reeseCoefs = hba::dsp::SvfCoefs::make(std::min(cutoff, 0.42 * fs_), q, fs_);

        double sumL = 0.0;
        double sumR = 0.0;

        constexpr std::array<double, kNumVoices> kPan{-0.26, 0.26, -0.42, 0.42};

        for (int vi = 0; vi < kNumVoices; ++vi) {
            auto& v = voices_[static_cast<std::size_t>(vi)];
            if (!v.gate && v.env < 1.0e-5)
                continue;

            v.env += ((v.gate ? 1.0 : 0.0) - v.env) * (v.gate ? atk : rel);
            v.pluckEnv *= pluckDecay_;

            const double bend = v.bendSmooth.step(channelBend_[static_cast<std::size_t>(v.channel - 1)], bendCoef_);
            const double freq = 440.0 * std::pow(2.0, (static_cast<double>(v.note) + bend - 69.0) / 12.0);
            const double inc = std::clamp(freq / fs_, 1.0e-5, 0.45);
            const double detInc = std::clamp(inc * 1.0055, 1.0e-5, 0.45);

            v.phase += inc;
            if (v.phase >= 1.0)
                v.phase -= 1.0;
            v.detPhase += detInc;
            if (v.detPhase >= 1.0)
                v.detPhase -= 1.0;
            v.subPhase += inc * 0.5;
            if (v.subPhase >= 1.0)
                v.subPhase -= 1.0;

            double voiceL = 0.0;
            double voiceR = 0.0;

            if (tone == AuditionTone::sub) {
                const double fund = std::sin(6.283185307179586 * v.phase);
                const double warm = 0.16 * std::sin(6.283185307179586 * v.phase * 2.0);
                const double s = (fund + warm) * v.env * v.vel * 0.85;
                voiceL = s;
                voiceR = s;
            } else if (tone == AuditionTone::pluck) {
                const double saw = 2.0 * v.phase - 1.0;
                const double plCutoff = std::clamp(cutoff * (0.45 + 1.8 * v.pluckEnv), 120.0, 0.42 * fs_);
                const auto coefs = hba::dsp::SvfCoefs::make(plCutoff, q * 0.85, fs_);
                const double body = v.filter.lowpass(coefs, v.shaper.process(saw * (1.2 + 0.4 * drive_)));
                const double amp = v.env * (0.28 + 0.72 * v.pluckEnv) * v.vel;
                const double pan = kPan[static_cast<std::size_t>(vi)];
                voiceL = body * amp * (1.0 - pan * 0.5);
                voiceR = body * amp * (1.0 + pan * 0.5);
            } else {
                // Reese: dual detuned saws into ADAA1 Tanh + resonant stereo SVF + clean mono sub.
                const double sawA = 2.0 * v.phase - 1.0;
                const double sawB = 2.0 * v.detPhase - 1.0;
                const double sub = std::sin(6.283185307179586 * v.subPhase);
                const double growl = v.filter.lowpass(reeseCoefs, v.shaper.process(0.55 * (sawA + sawB) * drive_));
                const double side = v.sideFilter.lowpass(reeseCoefs, (sawA - sawB) * 0.22);
                const double amp = v.env * v.vel;
                voiceL = (0.46 * sub + 0.54 * (growl + side)) * amp;
                voiceR = (0.46 * sub + 0.54 * (growl - side)) * amp;
            }

            sumL += voiceL * 0.62;
            sumR += voiceR * 0.62;
        }

        outL = static_cast<float>(std::tanh(sumL * g * 0.85));
        outR = static_cast<float>(std::tanh(sumR * g * 0.85));
    }

  private:
    struct Voice {
        double phase = 0.0;
        double detPhase = 0.0;
        double subPhase = 0.0;
        double env = 0.0;
        double pluckEnv = 0.0;
        double vel = 0.8;
        int note = 41;
        int channel = 1;
        bool gate = false;
        std::uint64_t age = 0;
        hba::dsp::Smoother bendSmooth{};
        hba::dsp::SvfState filter{};
        hba::dsp::SvfState sideFilter{};
        hba::dsp::Adaa1<hba::dsp::TanhShaper> shaper{};

        void reset() noexcept CL_NONBLOCKING {
            phase = detPhase = subPhase = 0.0;
            env = pluckEnv = 0.0;
            gate = false;
            age = 0;
            bendSmooth.snap(0.0);
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
    double kickPitchDecay_ = 0.999;
    double kickEnvDecay_ = 0.9998;
    double openHatDecay_ = 0.9997;
    double shortDrumDecay_ = 0.9995;
    double cutoffTarget_ = 900.0;
    double qTarget_ = 1.8;
    double drive_ = 1.8;

    std::array<Voice, kNumVoices> voices_{};
    std::array<double, 16> channelBend_{};
    std::uint64_t ageCounter_ = 0;

    bool drumMode_ = false;
    bool drumIsKick_ = true;
    bool drumIsHat_ = false;
    double drumEnv_ = 0.0;
    double drumVel_ = 0.8;
    double drumPitch_ = 160.0;
    double drumPhase_ = 0.0;
    double drumDecay_ = 0.999;
    std::uint32_t noiseState_ = 0x12345678U;

    hba::dsp::Smoother cutoffSmooth_{};
    hba::dsp::Smoother gainSmooth_{};
    hba::dsp::Smoother qSmooth_{};
    hba::dsp::SvfState drumFilter_{};
    hba::dsp::SvfCoefs hatCoefs_{};
    hba::dsp::SvfCoefs snareCoefs_{};
};

} // namespace cl
