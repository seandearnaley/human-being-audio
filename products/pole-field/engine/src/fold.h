// Second-order antiderivative anti-aliased (ADAA2) waveshaper for pre-, inter-,
// and post-lattice saturation, backed by the studio-shared hba::dsp::Adaa2.
#pragma once

#include "control.h"
#include "pf/params.h"

#include <hba/dsp/adaa.h>

#include <algorithm>
#include <cmath>

namespace pf::dsp {

class Saturator {
public:
    void prepare(double sampleRate) noexcept PF_NONBLOCKING {
        dc_.prepare(10.0, sampleRate);
        xfadeInc_ = 1.0 / std::max(1.0, 0.005 * sampleRate);  // 5 ms click-free shaper crossfade
        reset();
    }

    void reset() noexcept PF_NONBLOCKING {
        dc_.reset();
        tube_.reset();
        clip_.reset();
        fold_.reset();
        buchla_.reset();
        sine_.reset();
        x1_ = 0.0;
        u1_ = 0.0;
        u2_ = 0.0;
        offset1_ = 0.0;
        offset2_ = 0.0;
        active_ = FoldType::tube;
        prev_ = FoldType::tube;
        xfade_ = 1.0;
        primed_ = false;
    }

    double process(double x, double drive01, double asym01, FoldType type) noexcept PF_NONBLOCKING {
        // ADAA2 introduces an exact 1.0-sample integer group delay; keep the dry
        // reference time-aligned so low-drive blending and bypass transitions have
        // zero comb filtering or sample jumps.
        const double dry = x1_;
        x1_ = x;

        if (drive01 <= 1.0e-5) {
            u2_ = u1_;
            u1_ = x;
            offset2_ = 0.0;
            offset1_ = 0.0;
            active_ = type;
            prev_ = type;
            xfade_ = 1.0;
            primed_ = false;
            dc_.reset();
            return dry;
        }

        const double gain = dbToGain(drive01 * 24.0);
        const double offset = 0.45 * asym01 * drive01;
        const double u = gain * x + offset;

        if (!primed_) {
            active_ = type;
            prev_ = type;
            xfade_ = 1.0;
            u1_ = gain * dry + offset;
            u2_ = u1_;
            offset1_ = offset;
            offset2_ = offset;
            primeModel(active_, u1_, u2_);
            primed_ = true;
        } else if (type != active_) {
            prev_ = active_;
            active_ = type;
            xfade_ = 0.0;
            primeModel(active_, u1_, u2_);
        }

        // Subtract quiescent bias at ADAA2's exact triangular-kernel centroid
        // (offset + offset1 + offset2)/3 so modulating Drive/Asym on silence
        // cancels through first order (leaving only O(Δoffset^2) < -140 dB).
        const double offsetCentroid = (offset + offset1_ + offset2_) * (1.0 / 3.0);
        double shaped = tickModel(active_, u) - evalModel(active_, offsetCentroid);
        if (xfade_ < 1.0) {
            const double oldShaped = tickModel(prev_, u) - evalModel(prev_, offsetCentroid);
            shaped = (1.0 - xfade_) * oldShaped + xfade_ * shaped;
            xfade_ = std::min(1.0, xfade_ + xfadeInc_);
        }

        u2_ = u1_;
        u1_ = u;
        offset2_ = offset1_;
        offset1_ = offset;

        const double wet = dc_.process(shaped);
        const double blend = std::min(1.0, drive01 * 4.0);
        const double comp = 1.0 / (1.0 + 0.35 * drive01);
        return (1.0 - blend) * dry + blend * wet * comp;
    }

private:
    static double evalModel(FoldType type, double u) noexcept PF_NONBLOCKING {
        switch (type) {
            case FoldType::tube: return hba::dsp::TanhShaper::eval(u);
            case FoldType::clip: return hba::dsp::HardClipShaper::eval(u);
            case FoldType::fold: return hba::dsp::TriangleFoldShaper::eval(u);
            case FoldType::buchla: return hba::dsp::Buchla259Shaper::eval(u);
            case FoldType::sine: return hba::dsp::SineFoldShaper::eval(u);
        }
        return u;
    }

    void primeModel(FoldType type, double u1, double u2) noexcept PF_NONBLOCKING {
        switch (type) {
            case FoldType::tube: tube_.prime(u1, u2); return;
            case FoldType::clip: clip_.prime(u1, u2); return;
            case FoldType::fold: fold_.prime(u1, u2); return;
            case FoldType::buchla: buchla_.prime(u1, u2); return;
            case FoldType::sine: sine_.prime(u1, u2); return;
        }
    }

    double tickModel(FoldType type, double u) noexcept PF_NONBLOCKING {
        switch (type) {
            case FoldType::tube: return tube_.process(u);
            case FoldType::clip: return clip_.process(u);
            case FoldType::fold: return fold_.process(u);
            case FoldType::buchla: return buchla_.process(u);
            case FoldType::sine: return sine_.process(u);
        }
        return u;
    }

    DcBlocker dc_;
    hba::dsp::Adaa2<hba::dsp::TanhShaper> tube_ {};
    hba::dsp::Adaa2<hba::dsp::HardClipShaper> clip_ {};
    hba::dsp::Adaa2<hba::dsp::TriangleFoldShaper> fold_ {};
    hba::dsp::Adaa2<hba::dsp::Buchla259Shaper> buchla_ {};
    hba::dsp::Adaa2<hba::dsp::SineFoldShaper> sine_ {};

    double x1_ = 0.0;
    double u1_ = 0.0;
    double u2_ = 0.0;
    double offset1_ = 0.0;
    double offset2_ = 0.0;
    double xfade_ = 1.0;
    double xfadeInc_ = 1.0 / 960.0;
    FoldType active_ = FoldType::tube;
    FoldType prev_ = FoldType::tube;
    bool primed_ = false;
};

}
