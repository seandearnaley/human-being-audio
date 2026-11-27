// Output stage: product-specific growl/output clip thresholds on top of
// studio-shared hba::dsp::softClipLinked.
#pragma once

#include "tf/nonblocking.h"

#include <hba/dsp/limiter.h>

#include <algorithm>

namespace tf::dsp {

// The growl clipper: clip the mids, keep the sub clean.
inline constexpr double kGrowlClipKnee = 0.6;

inline double growlClip(double& left, double& right, double ceiling) noexcept TF_NONBLOCKING {
    return hba::dsp::softClipLinked(left, right, kGrowlClipKnee * ceiling, ceiling);
}

// Per-channel ADAA1 growl clipper at 4x: C^2-smooth (zero stereo max(|L|,|R|) kinks),
// bit-identical 0.5*(x + prevX) below knee regardless of ceiling, bit-exact mono when
// width == 0, and returns instantaneous stereo peak gain reduction for growlClipDb metering.
class GrowlClipAdaa {
  public:
    void reset() noexcept TF_NONBLOCKING {
        left_.reset();
        right_.reset();
    }

    double process(double& left, double& right, double ceiling) noexcept TF_NONBLOCKING {
        const double knee = kGrowlClipKnee * ceiling;
        const double peak = std::max(std::abs(left), std::abs(right));
        const double meterGain = peak <= knee ? 1.0 : hba::dsp::softClip(peak, knee, ceiling) / peak;
        left = left_.process(left, kGrowlClipKnee, ceiling);
        right = right_.process(right, kGrowlClipKnee, ceiling);
        return meterGain;
    }

  private:
    hba::dsp::SoftClipAdaa1 left_;
    hba::dsp::SoftClipAdaa1 right_;
};

// The output safety clip: transparent up to the ceiling when Limit is ON;
// transparent +6 dBFS knee / +12 dBFS cap when Limit is OFF.
inline constexpr double kOutputClipMaxKnee = 0.97;

inline void outputClip(double& left, double& right, double ceiling, double limitAmt = 1.0) noexcept TF_NONBLOCKING {
    const double lim = std::clamp(limitAmt, 0.0, 1.0);
    const double knee = lim * std::min(ceiling, kOutputClipMaxKnee) + (1.0 - lim) * 2.0;
    const double ceil = lim * 1.0 + (1.0 - lim) * 4.0;
    hba::dsp::softClipLinked(left, right, knee, ceil);
}
}
