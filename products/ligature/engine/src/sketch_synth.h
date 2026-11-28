// SPDX-License-Identifier: AGPL-3.0-or-later
// The sketch synth: four quick part voices and a three-piece kit, so Ligature plays well on its own.
// Allocation happens only in prepare(); everything else is [[clang::nonblocking]].
#pragma once

#include "lg/nonblocking.h"

#include <hba/dsp/limiter.h>
#include <hba/dsp/oscillator.h>
#include <hba/dsp/random.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lg {

enum class DrumKind : std::uint8_t { kick, hat, clap };

class SketchSynth {
  public:
    static constexpr int kVoices = 48;
    static constexpr int kDrums = 12;

    void prepare(double sampleRate) {
        sr_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        delayL_.assign(static_cast<std::size_t>(sr_ * 2.0), 0.0f);
        delayR_.assign(delayL_.size(), 0.0f);
        reset();
    }

    void reset() noexcept LG_NONBLOCKING {
        for (auto& v : voices_)
            v.active = false;
        for (auto& d : drums_)
            d.active = false;
        std::fill(delayL_.begin(), delayL_.end(), 0.0f);
        std::fill(delayR_.begin(), delayR_.end(), 0.0f);
        writePos_ = 0;
    }

    void setTempo(double bpm) noexcept LG_NONBLOCKING {
        // A dotted-eighth echo on the melody and arp.
        const double seconds = 60.0 / std::clamp(bpm, 40.0, 240.0) * 0.75;
        delaySamples_ = std::clamp(static_cast<int>(seconds * sr_), 1, static_cast<int>(delayL_.size()) - 1);
    }

    void noteOn(int role, int pitch, float velocity) noexcept LG_NONBLOCKING {
        Voice* slot = nullptr;
        for (auto& v : voices_)
            if (!v.active) {
                slot = &v;
                break;
            }
        if (slot == nullptr) { // steal the oldest
            slot = &voices_[0];
            for (auto& v : voices_)
                if (v.age > slot->age)
                    slot = &v;
        }
        Voice& v = *slot;
        v = Voice{};
        v.active = true;
        v.role = role;
        v.pitch = pitch;
        v.velocity = std::clamp(velocity, 0.0f, 1.0f);
        v.freq = 440.0 * std::pow(2.0, (pitch - 69) / 12.0);
        v.pan = role == 0   ? (static_cast<double>((pitch * 7) % 11) / 10.0 - 0.5) * 0.5
                : role == 3 ? ((counter_++ & 1u) != 0u ? 0.3 : -0.3)
                            : 0.0;
        const double vel = v.velocity;
        switch (role) {
        case 0: // chords: detuned saws and a triangle through a closing low-pass
            v.cutoff = 700.0 + 2400.0 * vel;
            v.cutoffTarget = 900.0 + 500.0 * vel;
            v.cutoffCoef = coef(0.25);
            v.peak = 0.05 * vel;
            v.attack = coef(0.004);
            v.sustain = 0.6;
            v.decay = coef(0.2);
            v.release = coef(0.12);
            v.q = 0.6;
            break;
        case 1: // bass: saw and sine
            v.cutoff = 280.0 + 700.0 * vel;
            v.cutoffTarget = 220.0 + 260.0 * vel;
            v.cutoffCoef = coef(0.12);
            v.peak = 0.2 * vel;
            v.attack = coef(0.003);
            v.sustain = 0.75;
            v.decay = coef(0.15);
            v.release = coef(0.04);
            v.q = 1.1;
            break;
        case 2: // melody: a soft bell
            v.cutoff = 4200.0;
            v.cutoffTarget = 4200.0;
            v.cutoffCoef = coef(1.0);
            v.peak = 0.085 * vel;
            v.attack = coef(0.003);
            v.sustain = 0.45;
            v.decay = coef(0.22);
            v.release = coef(0.14);
            v.q = 0.5;
            break;
        default: // arp: a short pluck
            v.cutoff = 2600.0;
            v.cutoffTarget = 900.0;
            v.cutoffCoef = coef(0.06);
            v.peak = 0.032 * vel;
            v.attack = coef(0.0015);
            v.sustain = 0.2;
            v.decay = coef(0.06);
            v.release = coef(0.05);
            v.q = 2.0;
            break;
        }
    }

    void noteOff(int role, int pitch) noexcept LG_NONBLOCKING {
        for (auto& v : voices_)
            if (v.active && !v.released && v.role == role && v.pitch == pitch)
                v.released = true;
    }

    void releaseAll() noexcept LG_NONBLOCKING {
        for (auto& v : voices_)
            v.released = true;
    }

    void drum(DrumKind kind, float velocity) noexcept LG_NONBLOCKING {
        Drum* slot = &drums_[0];
        for (auto& d : drums_)
            if (!d.active) {
                slot = &d;
                break;
            }
        *slot = Drum{};
        slot->active = true;
        slot->kind = kind;
        slot->velocity = std::clamp(velocity, 0.0f, 1.0f);
    }

    // Adds `count` samples into left/right (which the caller has cleared or filled).
    void render(float* left, float* right, int count, float gain) noexcept LG_NONBLOCKING {
        for (int i = 0; i < count; ++i) {
            double l = 0.0, r = 0.0, send = 0.0;
            for (auto& v : voices_) {
                if (!v.active)
                    continue;
                const double s = renderVoice(v);
                const double pl = std::sqrt(0.5 - v.pan), pr = std::sqrt(0.5 + v.pan);
                l += s * pl * 1.41421356;
                r += s * pr * 1.41421356;
                if (v.role >= 2)
                    send += s * 0.22;
            }
            for (auto& d : drums_) {
                if (!d.active)
                    continue;
                const double s = renderDrum(d);
                l += s;
                r += s;
            }
            // Ping-pong echo.
            const std::size_t size = delayL_.size();
            const std::size_t readPos = (writePos_ + size - static_cast<std::size_t>(delaySamples_)) % size;
            const double echoL = delayR_[readPos], echoR = delayL_[readPos];
            delayL_[writePos_] = static_cast<float>(send + echoL * 0.32);
            delayR_[writePos_] = static_cast<float>(echoR * 0.32);
            writePos_ = (writePos_ + 1) % size;
            l += echoL * 0.6;
            r += echoR * 0.6;

            l = hba::dsp::softClip(l * gain);
            r = hba::dsp::softClip(r * gain);
            left[i] += static_cast<float>(l);
            right[i] += static_cast<float>(r);
        }
    }

  private:
    struct Voice {
        bool active = false;
        bool released = false;
        int role = 0;
        int pitch = 60;
        float velocity = 0.8f;
        double freq = 440.0;
        double phase[3]{};
        double env = 0.0;
        bool attacking = true;
        double peak = 0.05, sustain = 0.6, attack = 0.01, decay = 0.01, release = 0.01;
        double cutoff = 1000.0, cutoffTarget = 1000.0, cutoffCoef = 0.001, q = 0.7;
        double pan = 0.0;
        hba::dsp::SvfState filter;
        std::uint32_t age = 0;
    };
    struct Drum {
        bool active = false;
        DrumKind kind = DrumKind::kick;
        float velocity = 1.0f;
        double t = 0.0;
        double phase = 0.0;
        hba::dsp::SvfState filter;
    };

    double coef(double seconds) const noexcept LG_NONBLOCKING { return 1.0 - std::exp(-1.0 / (seconds * sr_)); }

    double renderVoice(Voice& v) noexcept LG_NONBLOCKING {
        ++v.age;
        if (v.released) {
            v.env += (0.0 - v.env) * v.release;
            if (v.env < 1e-5) {
                v.active = false;
                return 0.0;
            }
        } else if (v.attacking) {
            v.env += (v.peak * 1.05 - v.env) * v.attack;
            if (v.env >= v.peak) {
                v.env = v.peak;
                v.attacking = false;
            }
        } else {
            v.env += (v.peak * v.sustain - v.env) * v.decay;
        }
        v.cutoff += (v.cutoffTarget - v.cutoff) * v.cutoffCoef;
        const double inc = v.freq / sr_;
        double x = 0.0;
        switch (v.role) {
        case 0:
            x = 0.55 * hba::dsp::polyBlepSaw(v.phase[0], inc * 0.99596) +
                0.55 * hba::dsp::polyBlepSaw(v.phase[1], inc * 1.00405) + 0.8 * triangle(v.phase[2]);
            advance(v.phase[0], inc * 0.99596);
            advance(v.phase[1], inc * 1.00405);
            advance(v.phase[2], inc);
            break;
        case 1:
            x = 0.6 * hba::dsp::polyBlepSaw(v.phase[0], inc) + 1.1 * std::sin(2.0 * hba::dsp::kPi * v.phase[1]);
            advance(v.phase[0], inc);
            advance(v.phase[1], inc);
            break;
        case 2:
            x = triangle(v.phase[0]) + 0.22 * std::sin(2.0 * hba::dsp::kPi * v.phase[1]) +
                0.05 * std::sin(2.0 * hba::dsp::kPi * v.phase[2]);
            advance(v.phase[0], inc);
            advance(v.phase[1], inc * 2.0);
            advance(v.phase[2], inc * 3.01);
            break;
        default:
            x = 0.55 * hba::dsp::polyBlepSquare(v.phase[0], inc) + 0.3 * triangle(v.phase[1]);
            advance(v.phase[0], inc);
            advance(v.phase[1], inc * 2.0);
            break;
        }
        const auto c = hba::dsp::SvfCoefs::make(v.cutoff, v.q, sr_);
        return v.filter.lowpass(c, x) * v.env;
    }

    double renderDrum(Drum& d) noexcept LG_NONBLOCKING {
        const double t = d.t;
        d.t += 1.0 / sr_;
        double out = 0.0;
        switch (d.kind) {
        case DrumKind::kick: {
            const double f = 46.0 + 104.0 * std::exp(-t / 0.035);
            d.phase += f / sr_;
            out = std::sin(2.0 * hba::dsp::kPi * d.phase) * 0.55 * std::exp(-t / 0.11);
            if (t > 0.4)
                d.active = false;
            break;
        }
        case DrumKind::hat: {
            const auto c = hba::dsp::SvfCoefs::make(7500.0, 0.7, sr_);
            out = d.filter.highpass(c, noise_.nextBipolar()) * 0.09 * std::exp(-t / 0.014);
            if (t > 0.08)
                d.active = false;
            break;
        }
        case DrumKind::clap: {
            const auto c = hba::dsp::SvfCoefs::make(1500.0, 0.8, sr_);
            double env = 0.0;
            for (const double start : {0.0, 0.012, 0.024})
                if (t >= start)
                    env += std::exp(-(t - start) / (start < 0.02 ? 0.006 : 0.05));
            out = d.filter.bandpass(c, noise_.nextBipolar()) * 0.18 * env;
            if (t > 0.25)
                d.active = false;
            break;
        }
        }
        return out * d.velocity;
    }

    static double triangle(double phase) noexcept LG_NONBLOCKING { return 1.0 - 4.0 * std::abs(phase - 0.5); }
    static void advance(double& phase, double inc) noexcept LG_NONBLOCKING {
        phase += inc;
        if (phase >= 1.0)
            phase -= 1.0;
    }

    double sr_ = 48000.0;
    std::array<Voice, kVoices> voices_{};
    std::array<Drum, kDrums> drums_{};
    std::vector<float> delayL_, delayR_;
    std::size_t writePos_ = 0;
    int delaySamples_ = 1000;
    std::uint32_t counter_ = 0;
    hba::dsp::XorShift32 noise_;
};

} // namespace lg
