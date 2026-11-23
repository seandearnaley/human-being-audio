// Output stage: product-specific growl/output clip thresholds on top of
// studio-shared hba::dsp::Limiter and softClipLinked.
#pragma once

#include "control.h"
#include "tf/nonblocking.h"

#include <hba/dsp/limiter.h>

#include <algorithm>

namespace tf::dsp {

using hba::dsp::Limiter;
using hba::dsp::MovingMinimum;
using hba::dsp::TruePeakDetector;
using hba::dsp::kSoftClipKnee;
using hba::dsp::softClip;
using hba::dsp::softClipLinked;

// The growl clipper: clip the mids, keep the sub clean.
inline constexpr double kGrowlClipKnee = 0.6;

inline double growlClip(double& left, double& right, double ceiling) noexcept TF_NONBLOCKING {
    return softClipLinked(left, right, kGrowlClipKnee * ceiling, ceiling);
}

// The output safety clip: transparent up to the ceiling when Limit is ON;
// transparent +6 dBFS knee / +12 dBFS cap when Limit is OFF.
inline constexpr double kOutputClipMaxKnee = 0.97;

inline void outputClip(double& left, double& right, double ceiling, double limitAmt = 1.0) noexcept TF_NONBLOCKING {
    const double lim = std::clamp(limitAmt, 0.0, 1.0);
    const double knee = lim * std::min(ceiling, kOutputClipMaxKnee) + (1.0 - lim) * 2.0;
    const double ceil = lim * 1.0 + (1.0 - lim) * 4.0;
    softClipLinked(left, right, knee, ceil);
}
}
