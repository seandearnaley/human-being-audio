// Where the two reese voices sit relative to the master: the cents detune, or,
// with Beat lock, a constant offset in Hz. The engine plays this and tf-render
// uses it to set the off-harmonic tolerance, so both read one definition.
#pragma once

#include "tf/feel.h"
#include "tf/nonblocking.h"
#include "tf/params.h"

#include <hba/dsp/unison.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tf {

// Each voice's share of the Beat lock offset: voice A runs at f0 ratioA + d sA,
// voice B at f0 ratioB + d sB, with sA - sB = +-1 so the fundamentals beat
// exactly once per division. The magnitudes are in proportion to |aDetune| and
// |bDetune|. The direction follows the larger detune: A moves with the sign of
// (aDetune - bDetune) and B the other way, so opposite detunes (the usual
// reese) keep their own signs. Both zero (or equal) split +0.5 / -0.5.
struct BeatLockShares {
    double a = 0.5, b = -0.5;
};

inline BeatLockShares beatLockShares(double aDetune, double bDetune) noexcept TF_NONBLOCKING {
    const double magA = std::abs(aDetune), magB = std::abs(bDetune);
    const double total = magA + magB;
    if (total <= 1e-9) return {};
    const double direction = aDetune - bDetune >= 0.0 ? 1.0 : -1.0;
    return { direction * magA / total, -direction * magB / total };
}

// Beat frequency in Hz for Beat lock: one beat per division at `bpm`.
inline double beatLockHz(double bpm, double beat) noexcept TF_NONBLOCKING {
    const int index = feel::beatLockIndex(static_cast<float>(beat), static_cast<int>(kBeatLockQuarters.size()));
    return bpm / 60.0 / kBeatLockQuarters[static_cast<std::size_t>(index)];
}

// Each voice's frequency offset in Hz from f0 x its ratio, for the parameter
// values as given (no lanes).
struct VoiceOffsets {
    double a = 0.0, b = 0.0;
};

inline VoiceOffsets voiceOffsetsHz(const Params& p, double f0, double bpm) noexcept TF_NONBLOCKING {
    const double beat = p[ParamId::beat];
    if (p.on(ParamId::beatLock)) {
        const double delta = beatLockHz(bpm, beat);
        const auto s = beatLockShares(p[ParamId::aDetune], p[ParamId::bDetune]);
        return { delta * s.a, delta * s.b };
    }
    const auto offset = [&](ParamId detune, ParamId ratio) {
        const double cents = feel::detuneCents(p[detune], static_cast<float>(beat));
        return f0 * p[ratio] * (std::exp2(cents / 1200.0) - 1.0);
    };
    return { offset(ParamId::aDetune, ParamId::aRatio), offset(ParamId::bDetune, ParamId::bRatio) };
}

// The stack (spec, "Revision 2, pass 1"): side voice j sits Szabo's offset j
// times this many cents at Spread 1 and Beat 1 (Spread squared, for a fine low
// end). Under Beat lock it sits offset j x Spread beats per division away instead.
inline constexpr double kStackMaxCents = 60.0;

inline double stackCents(double offset, double spread, double beat) noexcept TF_NONBLOCKING {
    return offset * kStackMaxCents * spread * spread * beat;
}

// How far each voice's partials sit from the note's harmonics. A Sine voice is
// the master's PM sidebands around its carrier, so every partial moves by the
// carrier's offset: `flatHz`. Saw, Pulse and PD voices multiply their
// fundamental's offset by the harmonic number: `perHarmonicHz` is the offset at
// the first harmonic of the note. Both include the widest stack side voice.
struct HarmonicSpread {
    double flatHz = 0.0;
    double perHarmonicHz = 0.0;
};

inline HarmonicSpread harmonicSpreadHz(const Params& p, double f0, double bpm) noexcept TF_NONBLOCKING {
    const auto offsets = voiceOffsetsHz(p, f0, bpm);
    const int count = std::clamp(p.choice(ParamId::stack), 0, hba::dsp::kMaxUnison - 1) + 1;
    const double spread = std::clamp(static_cast<double>(p[ParamId::spread]), 0.0, 1.0);
    const double beat = p[ParamId::beat];
    const bool locked = p.on(ParamId::beatLock);
    HarmonicSpread s;
    const auto voice = [&](double offsetHz, ParamId ratioId, ParamId modelId) {
        const double ratio = std::max(1e-3, static_cast<double>(p[ratioId]));
        double side = 0.0;
        for (std::size_t j = 1; j < static_cast<std::size_t>(count); ++j) {
            const double o = hba::dsp::kSzaboOffsets[j];
            side =
                std::max(side, locked ? std::abs(o) * spread * beatLockHz(bpm, beat)
                                      : f0 * ratio * std::abs(std::exp2(stackCents(o, spread, beat) / 1200.0) - 1.0));
        }
        const double total = std::abs(offsetHz) + side;
        if (p.choice(modelId) == static_cast<int>(VoiceModel::sine))
            s.flatHz = std::max(s.flatHz, total);
        else
            s.perHarmonicHz = std::max(s.perHarmonicHz, total / ratio);
    };
    voice(offsets.a, ParamId::aRatio, ParamId::aModel);
    voice(offsets.b, ParamId::bRatio, ParamId::bModel);
    return s;
}
}
