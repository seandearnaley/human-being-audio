// SPDX-License-Identifier: AGPL-3.0-or-later
// Lightweight, [[clang::nonblocking]] built-in audition voice for VST3/Standalone
// preview. Reuses hba::dsp primitives (SVF, ADAA1 Tanh, Gain, Smoother).
#pragma once

#include "cl/nonblocking.h"

#include <hba/dsp/adaa.h>
#include <hba/dsp/control.h>
#include <hba/dsp/gain.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cl {

class AuditionSynth {
  public:
    void prepare(double sampleRate) noexcept CL_NONBLOCKING {
        fs_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        cutoffCoef_ = hba::dsp::onePoleCoef(0.008, 1.0 / fs_);
        gainCoef_ = hba::dsp::onePoleCoef(0.012, 1.0 / fs_);
        bendCoef_ = hba::dsp::onePoleCoef(0.006, 1.0 / fs_);
        reset();
    }

    void reset() noexcept CL_NONBLOCKING {
        phase_ = 0.0;
        subPhase_ = 0.0;
        env_ = 0.0;
        gate_ = false;
        drumEnv_ = 0.0;
        drumPitch_ = 160.0;
        drumMode_ = false;
        drumIsKick_ = true;
        drumIsHat_ = false;
        cutoffTarget_ = 900.0;
        bendTarget_ = 0.0;
        filter_.reset();
        drumFilter_.reset();
        shaper_.reset();
        cutoffSmooth_.snap(900.0);
        gainSmooth_.snap(0.0);
        bendSmooth_.snap(0.0);
    }

    void noteOn(int note, int velocity, bool isDrum) noexcept CL_NONBLOCKING {
        const double v = static_cast<double>(std::clamp(velocity, 1, 127)) / 127.0;
        drumMode_ = isDrum;
        if (isDrum) {
            drumVel_ = v;
            drumEnv_ = 1.0;
            drumIsKick_ = (note == 36);
            drumIsHat_ = (note == 42 || note == 46);
            drumPitch_ = drumIsKick_ ? 165.0 : (note == 38 ? 220.0 : 340.0);
            drumDecay_ = drumIsKick_ ? std::exp(-1.0 / (0.11 * fs_))
                                     : (note == 46 ? std::exp(-1.0 / (0.09 * fs_)) : std::exp(-1.0 / (0.045 * fs_)));
            return;
        }
        note_ = std::clamp(note, 12, 120);
        vel_ = v;
        gate_ = true;
    }

    void noteOff(int note) noexcept CL_NONBLOCKING {
        if (!drumMode_ && note == note_)
            gate_ = false;
    }

    void allNotesOff() noexcept CL_NONBLOCKING { gate_ = false; }

    void setBendSemitones(double semitones) noexcept CL_NONBLOCKING {
        bendTarget_ = std::clamp(semitones, -48.0, 48.0);
    }

    void setBrightness(double norm01) noexcept CL_NONBLOCKING {
        const double clamped = std::clamp(norm01, 0.0, 1.0);
        cutoffTarget_ = 180.0 * std::pow(20.0, clamped);
    }

    void setDrive(double norm01) noexcept CL_NONBLOCKING { drive_ = 1.0 + 3.2 * std::clamp(norm01, 0.0, 1.0); }

    void renderSample(float gainDb, bool enabled, float& outL, float& outR) noexcept CL_NONBLOCKING {
        const double targetGain = enabled ? hba::dsp::dbToGain(static_cast<double>(gainDb)) : 0.0;
        const double g = gainSmooth_.step(targetGain, gainCoef_);

        if (g < 1.0e-5 && env_ < 1.0e-5 && drumEnv_ < 1.0e-5) {
            outL = 0.0f;
            outR = 0.0f;
            return;
        }

        double sample = 0.0;
        if (drumMode_) {
            if (drumEnv_ > 1.0e-5) {
                noiseState_ = noiseState_ * 1664525U + 1013904223U;
                const double noise = (static_cast<double>(noiseState_) / 2147483648.0) - 1.0;
                if (drumIsKick_) {
                    drumPitch_ = 46.0 + (drumPitch_ - 46.0) * std::exp(-1.0 / (0.025 * fs_));
                    subPhase_ += drumPitch_ / fs_;
                    if (subPhase_ >= 1.0)
                        subPhase_ -= 1.0;
                    sample = std::sin(6.283185307179586 * subPhase_) * drumEnv_ * drumVel_ * 1.1;
                } else {
                    const auto c = hba::dsp::SvfCoefs::make(drumIsHat_ ? 6800.0 : 1800.0, 1.4, fs_);
                    const double f = drumIsHat_ ? drumFilter_.highpass(c, noise) : drumFilter_.bandpass(c, noise);
                    sample = f * drumEnv_ * drumVel_ * 0.9;
                }
                drumEnv_ *= drumDecay_;
            }
        } else {
            const double atk = 1.0 - std::exp(-1.0 / (0.004 * fs_));
            const double rel = 1.0 - std::exp(-1.0 / (0.022 * fs_));
            env_ += ((gate_ ? 1.0 : 0.0) - env_) * (gate_ ? atk : rel);

            const double bend = bendSmooth_.step(bendTarget_, bendCoef_);
            const double freq = 440.0 * std::pow(2.0, (static_cast<double>(note_) + bend - 69.0) / 12.0);
            const double inc = std::clamp(freq / fs_, 1.0e-5, 0.45);
            phase_ += inc;
            if (phase_ >= 1.0)
                phase_ -= 1.0;
            subPhase_ += inc * 0.5;
            if (subPhase_ >= 1.0)
                subPhase_ -= 1.0;

            const double saw = 2.0 * phase_ - 1.0;
            const double sub = std::sin(6.283185307179586 * subPhase_);
            const double cutoff = cutoffSmooth_.step(cutoffTarget_, cutoffCoef_);
            const auto coefs = hba::dsp::SvfCoefs::make(cutoff, 2.4, fs_);
            const double growl = filter_.lowpass(coefs, shaper_.process(saw * drive_));
            sample = (0.48 * sub + 0.52 * growl) * env_ * vel_;
        }

        const float y = static_cast<float>(std::tanh(sample * g * 0.85));
        outL = y;
        outR = y;
    }

  private:
    double fs_ = 48000.0;
    double cutoffCoef_ = 0.003;
    double gainCoef_ = 0.002;
    double bendCoef_ = 0.004;
    double cutoffTarget_ = 900.0;
    double bendTarget_ = 0.0;

    double phase_ = 0.0;
    double subPhase_ = 0.0;
    double env_ = 0.0;
    double vel_ = 0.8;
    double drive_ = 1.8;
    int note_ = 41;
    bool gate_ = false;

    bool drumMode_ = false;
    bool drumIsKick_ = true;
    bool drumIsHat_ = false;
    double drumEnv_ = 0.0;
    double drumVel_ = 0.8;
    double drumPitch_ = 160.0;
    double drumDecay_ = 0.999;
    std::uint32_t noiseState_ = 0x12345678U;

    hba::dsp::Smoother cutoffSmooth_{};
    hba::dsp::Smoother gainSmooth_{};
    hba::dsp::Smoother bendSmooth_{};
    hba::dsp::SvfState filter_{};
    hba::dsp::SvfState drumFilter_{};
    hba::dsp::Adaa1<hba::dsp::TanhShaper> shaper_{};
};

} // namespace cl
