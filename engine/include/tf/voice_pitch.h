// Where the two reese voices sit relative to the master: the cents detune, or,
// with Beat lock, a constant offset in Hz. The engine plays this and tf-render
// uses it to set the off-harmonic tolerance, so both read one definition.
#pragma once

#include "tf/feel.h"
#include "tf/nonblocking.h"
#include "tf/params.h"

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

// The sub/growl split in Hz: Sub split (a harmonic number) times the note,
// kept within 30..400 Hz. The engine uses it for the sub's low-pass, the
// growl's high-passes, Weight's pivot and Fit; tf-render and the tests measure
// the sub band at the same frequency.
inline constexpr double kSubSplitMinHz = 30.0;
inline constexpr double kSubSplitMaxHz = 400.0;
inline double subSplitHz(double subSplit, double f0) noexcept TF_NONBLOCKING {
    return std::clamp(subSplit * f0, kSubSplitMinHz, kSubSplitMaxHz);
}

}
