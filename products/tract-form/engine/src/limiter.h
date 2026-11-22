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

// The output safety clip: transparent up to the ceiling, then a short shoulder to 0 dBFS.
inline constexpr double kOutputClipMaxKnee = 0.97;

inline void outputClip(double& left, double& right, double ceiling) noexcept TF_NONBLOCKING {
    softClipLinked(left, right, std::min(ceiling, kOutputClipMaxKnee), 1.0);
}

}
