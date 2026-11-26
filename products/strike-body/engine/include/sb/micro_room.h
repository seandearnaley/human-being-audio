// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <hba/dsp/biquad.h>
#include <hba/dsp/control.h>
#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sb {

// Fixed-capacity power-of-two fractional delay ring buffer (>10 ms at 192 kHz).
struct DelayBuf {
    static constexpr int kSize = 2048;
    static constexpr int kMask = kSize - 1;
    std::array<double, kSize> buf{};
    int wp = 0;

    void reset() noexcept HBA_NONBLOCKING {
        buf.fill(0.0);
        wp = 0;
    }
    void push(double x) noexcept HBA_NONBLOCKING {
        buf[static_cast<std::size_t>(wp)] = x;
        wp = (wp + 1) & kMask;
    }
    double readFrac(double delaySamples) const noexcept HBA_NONBLOCKING {
        const double d = std::clamp(delaySamples, 0.0, static_cast<double>(kSize - 4));
        const int dInt = static_cast<int>(d);
        const double frac = d - static_cast<double>(dInt);
        const int i0 = (wp - 1 - dInt) & kMask;
        const int i1 = (i0 - 1) & kMask;
        return buf[static_cast<std::size_t>(i0)] * (1.0 - frac) + buf[static_cast<std::size_t>(i1)] * frac;
    }
    double readInt(int delaySamples) const noexcept HBA_NONBLOCKING {
        const int d = std::clamp(delaySamples, 0, kSize - 2);
        return buf[static_cast<std::size_t>((wp - 1 - d) & kMask)];
    }
};

// Lightweight zero-alloc biquad for Tone Scoop (380 Hz bell + 6.2 kHz shelf).
struct Biquad {
    hba::dsp::BiquadCoefs coefs{};
    hba::dsp::BiquadState state{};

    void reset() noexcept HBA_NONBLOCKING { state.reset(); }
    void setPeak(double hz, double q, double gainDb, double sr) noexcept HBA_NONBLOCKING {
        coefs = hba::dsp::BiquadCoefs::peak(hz, std::max(0.25, q), gainDb, sr);
    }
    void setHighShelf(double hz, double slope, double gainDb, double sr) noexcept HBA_NONBLOCKING {
        coefs = hba::dsp::BiquadCoefs::highShelf(hz, std::max(0.2, slope), gainDb, sr);
    }
    double process(double x) noexcept HBA_NONBLOCKING { return state.process(coefs, x); }
};

// Tight zero-alloc 4-line Hadamard Stereo Early-Reflection Micro-Room (8-35 ms).
struct MicroRoom {
    static constexpr int kLineSize = 8192; // > 42 ms even at 192 kHz
    static constexpr int kLineMask = kLineSize - 1;
    std::array<std::array<double, kLineSize>, 4> lines{};
    std::array<int, 4> wp{};
    std::array<int, 4> lens{397, 659, 1013, 1531};
    std::array<double, 4> lpState{};
    double hpZ1L = 0.0, hpX1L = 0.0;
    double hpZ1R = 0.0, hpX1R = 0.0;
    double hpCoef = 0.97;
    double lpCoef = 0.35;

    void prepare(double sr) noexcept HBA_NONBLOCKING {
        const double s = sr / 48000.0;
        lens[0] = std::clamp(static_cast<int>(std::lround(397.0 * s)), 64, kLineSize - 8);   // ~8.3 ms
        lens[1] = std::clamp(static_cast<int>(std::lround(659.0 * s)), 96, kLineSize - 8);   // ~13.7 ms
        lens[2] = std::clamp(static_cast<int>(std::lround(1013.0 * s)), 128, kLineSize - 8); // ~21.1 ms
        lens[3] = std::clamp(static_cast<int>(std::lround(1531.0 * s)), 160, kLineSize - 8); // ~31.9 ms
        hpCoef = std::exp(-hba::dsp::kTwoPi * 220.0 / sr);
        lpCoef = std::clamp(1.0 - std::exp(-hba::dsp::kTwoPi * 7200.0 / sr), 0.05, 0.95);
        reset();
    }
    void reset() noexcept HBA_NONBLOCKING {
        for (auto& ln : lines)
            ln.fill(0.0);
        wp.fill(0);
        lpState.fill(0.0);
        hpZ1L = hpX1L = hpZ1R = hpX1R = 0.0;
    }
    void process(double inL, double inR, double amt, double fb, double& outL, double& outR) noexcept HBA_NONBLOCKING {
        // High-pass input at 220 Hz so sub-bass stays pure, dry, and phase-locked!
        const double hiL = hpCoef * (hpZ1L + inL - hpX1L);
        hpX1L = inL;
        hpZ1L = hiL;
        const double hiR = hpCoef * (hpZ1R + inR - hpX1R);
        hpX1R = inR;
        hpZ1R = hiR;

        std::array<double, 4> d{};
        for (int k = 0; k < 4; ++k) {
            const auto ki = static_cast<std::size_t>(k);
            const int rp = (wp[ki] - lens[ki]) & kLineMask;
            const double raw = lines[ki][static_cast<std::size_t>(rp)];
            lpState[ki] += lpCoef * (raw - lpState[ki]);
            d[ki] = lpState[ki];
        }

        // Orthogonal 4x4 Hadamard diffusion matrix (energy-preserving)
        const double h0 = 0.5 * (d[0] + d[1] + d[2] + d[3]);
        const double h1 = 0.5 * (d[0] - d[1] + d[2] - d[3]);
        const double h2 = 0.5 * (d[0] + d[1] - d[2] - d[3]);
        const double h3 = 0.5 * (d[0] - d[1] - d[2] + d[3]);

        const double injL = hiL * 0.52;
        const double injR = hiR * 0.52;
        const std::array<double, 4> next{injL + fb * h0, injR + fb * h1, injL + fb * h2, injR + fb * h3};
        for (int k = 0; k < 4; ++k) {
            const auto ki = static_cast<std::size_t>(k);
            lines[ki][static_cast<std::size_t>(wp[ki])] = next[ki];
            wp[ki] = (wp[ki] + 1) & kLineMask;
        }

        const double wetL = (d[0] + 0.72 * d[2] - 0.35 * d[1]) * amt * 0.58;
        const double wetR = (d[1] + 0.72 * d[3] - 0.35 * d[0]) * amt * 0.58;
        outL = inL + wetL;
        outR = inR + wetR;
    }
};

} // namespace sb
