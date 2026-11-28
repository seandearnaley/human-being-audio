// SPDX-License-Identifier: AGPL-3.0-or-later
// The reese voices' oscillator models and the filter that replaced Reach's
// fixed low-pass (spec, "Revision 2, pass 1").
//
// Models, each phase-modulated by the master exactly as the sine always was:
//   Sine   the original warped sine, sin(phi + 0.85 shape sin phi) blended in
//   Saw    a PolyBLEP saw whose ramp Shape bends by the same warp
//   Pulse  a PolyBLEP pulse, width 0.5 down to 0.05 with Shape, DC removed
//   PD     Casio-style resonant phase distortion: (1 - t)(1 - cos 2 pi k t),
//          k = 2^(4 shape), restarted every cycle, so it stays harmonic
// The filter's cutoff is Reach (harmonics of the note). Clean is Reach's
// original 24 dB/oct Butterworth and ignores Resonance and Drive.
#pragma once

#include "harmonic.h"

#include "tf/nonblocking.h"
#include "tf/params.h"

#include <hba/dsp/control.h>
#include <hba/dsp/oscillator.h>
#include <hba/dsp/svf.h>
#include <hba/dsp/va_filter.h>

#include <algorithm>
#include <cmath>

namespace tf::dsp {

// ---- oscillator models -------------------------------------------------------

// The original oscillator, now the Sine model: a pure PM sine blended with the
// warped sine sin(rad + k sin rad), the warp eased off near Nyquist. Under Beat
// lock it warps against the locked phase so every harmonic beats at one rate.
inline double warpedSine(double shp, double phi, double phiLock, double mod, double inc,
                         bool locked) noexcept TF_NONBLOCKING {
    const double rad = phi + mod;
    const double s1 = std::sin(rad);
    if (shp <= 1.0e-5)
        return s1;
    const double nyqGuard = std::clamp((0.36 - 4.0 * inc) / 0.10, 0.0, 1.0);
    if (locked) {
        const double warp = 0.88 * shp * nyqGuard;
        return std::sin(phi + warp * std::sin(phiLock) + mod);
    }
    const double warp = 0.85 * shp * nyqGuard;
    const double rich = std::sin(rad + warp * s1);
    return (1.0 - shp) * s1 + shp * rich;
}

inline constexpr double kWarpDepth = 0.85; // the original Shape warp's depth
inline constexpr double kPdMaxOctaves = 4.0;

// Model-dependent constants, recomputed per control period.
struct ModelShape {
    double bend = 0.0;    // Saw: warp depth
    double width = 0.5;   // Pulse: duty cycle
    double pulseDc = 0.0; // Pulse: mean of the raw wave
    double pdK = 1.0;     // PD: resonance harmonic
    double pdDc = 0.5;    // PD: mean of the raw wave
};

inline ModelShape modelShape(double shape) noexcept TF_NONBLOCKING {
    const double s = std::clamp(shape, 0.0, 1.0);
    ModelShape m;
    m.bend = kWarpDepth * s;
    m.width = 0.5 - 0.45 * s;
    m.pulseDc = 2.0 * m.width - 1.0;
    m.pdK = std::exp2(kPdMaxOctaves * s);
    // Mean of (1 - t)(1 - cos 2 pi k t) over a cycle.
    const double w = hba::dsp::kTwoPi * m.pdK;
    m.pdDc = 0.5 - (1.0 - std::cos(w)) / (w * w);
    return m;
}

// One sample of a non-sine model. `t` is the phase in cycles, [0, 1), and `dt`
// how far it moved this sample (cycles, either direction under deep PM).
inline double modelSample(VoiceModel model, const ModelShape& m, double t, double dt) noexcept TF_NONBLOCKING {
    const double step = std::min(std::abs(dt), 0.5);
    switch (model) {
    case VoiceModel::saw: {
        // The warp leaves t = 0 and t = 1 in place, so the step stays at the wrap.
        const double bent = t + m.bend / hba::dsp::kTwoPi * std::sin(hba::dsp::kTwoPi * t);
        return (2.0 * bent - 1.0) - hba::dsp::polyBlep(t, step);
    }
    case VoiceModel::pulse:
        return hba::dsp::polyBlepPulse(t, step, m.width) - m.pulseDc;
    case VoiceModel::pd: {
        const double raw = (1.0 - t) * (1.0 - std::cos(hba::dsp::kTwoPi * m.pdK * t));
        return 1.2 * (raw - m.pdDc);
    }
    case VoiceModel::sine:
        break;
    }
    return std::sin(hba::dsp::kTwoPi * t);
}

// ---- filter ------------------------------------------------------------------

// Per-sample filter controls: Reach's pre-warped cutoff, resonance 0..1 and the
// drive gain into the saturated types, with the coefficients the Clean and SVF
// types need, worked out once for both voices.
struct FilterControls {
    double g = 0.0;
    double res = 0.0;
    double drive = 1.0;
    ReachCoefs clean{};
    hba::dsp::SvfCoefs svf{};
};

// Resonance to each type's feedback or damping.
inline double ladderFeedback(double res) noexcept TF_NONBLOCKING {
    return 1.05 * hba::dsp::TptLadder::kSelfOscillation * std::clamp(res, 0.0, 1.0);
}
inline double korg35Feedback(double res) noexcept TF_NONBLOCKING {
    return 1.05 * hba::dsp::TptKorg35::kSelfOscillation * std::clamp(res, 0.0, 1.0);
}
// SVF damping k = 1 / Q, Q from Butterworth (0.707) up to about 35.
inline double svfDamping(double res) noexcept TF_NONBLOCKING {
    return std::sqrt(2.0) * (1.0 - 0.98 * std::clamp(res, 0.0, 1.0));
}

inline bool usesSvf(FilterType t) noexcept TF_NONBLOCKING { return t == FilterType::lowpass || t == FilterType::notch; }

// The controls for one sample, with only the coefficients the playing types use.
inline FilterControls filterControls(double g, double res, double drive, FilterType from,
                                     FilterType to) noexcept TF_NONBLOCKING {
    FilterControls c{g, res, drive, {}, {}};
    if (from == FilterType::clean || to == FilterType::clean)
        c.clean = ReachCoefs::make(g);
    if (usesSvf(from) || usesSvf(to))
        c.svf = hba::dsp::SvfCoefs::fromGK(g, svfDamping(res));
    return c;
}

// One channel's filter, holding every type's state so a type change can
// crossfade from the old type to the new one.
class VoiceFilter {
  public:
    void reset() noexcept TF_NONBLOCKING {
        clean_.reset();
        ladder_.reset();
        korg_.reset();
        lowpass_.reset();
        notch_.reset();
    }
    void resetType(FilterType type) noexcept TF_NONBLOCKING {
        switch (type) {
        case FilterType::clean:
            clean_.reset();
            break;
        case FilterType::ladder:
            ladder_.reset();
            break;
        case FilterType::ms20:
            korg_.reset();
            break;
        case FilterType::lowpass:
            lowpass_.reset();
            break;
        case FilterType::notch:
            notch_.reset();
            break;
        }
    }

    double run(FilterType type, const FilterControls& c, double x) noexcept TF_NONBLOCKING {
        switch (type) {
        case FilterType::clean:
            return clean_.process(c.clean, x);
        case FilterType::ladder: {
            const double k = ladderFeedback(c.res);
            // Make up the ladder's passband loss, 1 / (1 + k), part way.
            return (1.0 + 0.4 * k) * ladder_.process(x, c.g, k, c.drive);
        }
        case FilterType::ms20:
            return korg_.process(x, c.g, korg35Feedback(c.res), c.drive);
        case FilterType::lowpass:
            return lowpass_.lowpass(c.svf, hba::dsp::softSaturate(c.drive * x, kSvfLimit));
        case FilterType::notch: {
            const double in = hba::dsp::softSaturate(c.drive * x, kSvfLimit);
            double band = 0.0, low = 0.0;
            notch_.tick(c.svf, in, band, low);
            return in - c.svf.k * band;
        }
        }
        return x;
    }

    // `mix` runs 0 -> 1 across a type change from `from` to `to`.
    double process(FilterType from, FilterType to, double mix, const FilterControls& c,
                   double x) noexcept TF_NONBLOCKING {
        if (to == FilterType::clean && (mix >= 1.0 || from == to))
            return clean_.process(c.clean, x); // the common case, kept inline
        const double now = run(to, c, x);
        if (mix >= 1.0 || from == to)
            return now;
        return mix * now + (1.0 - mix) * run(from, c, x);
    }

  private:
    static constexpr double kSvfLimit = 1.5;
    ReachFilter clean_;
    hba::dsp::TptLadder ladder_;
    hba::dsp::TptKorg35 korg_;
    hba::dsp::SvfState lowpass_, notch_;
};

} // namespace tf::dsp
