// The fold: x' = g x + 0.5 asym through a studio-shared ADAA2 waveshaper
// (hba::dsp::Adaa2). ADAA2 provides -12 dB/oct aliasing attenuation, cached
// F2/D1 state, exact 1-sample dry alignment with `warp`, and centroid bias
// subtraction so modulating Drive/Asym on silence emits zero CV thump.
#pragma once

#include "control.h"
#include "tf/params.h"

#include <hba/dsp/adaa.h>

#include <algorithm>
#include <cmath>

namespace tf::dsp {

inline double shape(FoldType type, double u) noexcept TF_NONBLOCKING {
    switch (type) {
        case FoldType::fold: return hba::dsp::TriangleFoldShaper::eval(u);
        case FoldType::sine: return hba::dsp::SineFoldShaper::eval(u);
        case FoldType::tube: return hba::dsp::TanhShaper::eval(u);
        case FoldType::buchla: return hba::dsp::Buchla259Shaper::eval(u);
        case FoldType::clip: return hba::dsp::HardClipShaper::eval(u);
    }
    return u;
}

inline double shapeAntiderivative(FoldType type, double u) noexcept TF_NONBLOCKING {
    switch (type) {
        case FoldType::fold: return hba::dsp::TriangleFoldShaper::ad1(u);
        case FoldType::sine: return hba::dsp::SineFoldShaper::ad1(u);
        case FoldType::tube: return hba::dsp::TanhShaper::ad1(u);
        case FoldType::buchla: return hba::dsp::Buchla259Shaper::ad1(u);
        case FoldType::clip: return hba::dsp::HardClipShaper::ad1(u);
    }
    return 0.5 * u * u;
}

inline constexpr double kAdaaEpsilon = hba::dsp::Adaa1<hba::dsp::TanhShaper>::kEps;

inline double adaa(FoldType type, double u, double u1) noexcept TF_NONBLOCKING {
    const double d = u - u1;
    if (std::abs(d) < kAdaaEpsilon) return shape(type, 0.5 * (u + u1));
    return (shapeAntiderivative(type, u) - shapeAntiderivative(type, u1)) / d;
}

class Folder {
public:
    void prepare(double sampleRate) noexcept TF_NONBLOCKING {
        dc_.prepare(10.0, sampleRate);
        reset();
    }
    void reset() noexcept TF_NONBLOCKING {
        dc_.reset();
        fold_.reset();
        sine_.reset();
        tube_.reset();
        buchla_.reset();
        clip_.reset();
        x1_ = 0.0;
        u1_ = 0.0;
        u2_ = 0.0;
        offset1_ = 0.0;
        offset2_ = 0.0;
        activeTo_ = FoldType::fold;
        activeFrom_ = FoldType::fold;
        primed_ = false;
    }

    // `mix` crossfades from shaper `from` (0) to `to` (1) when the type changes.
    double process(double x, double gain, double offset, double warp, FoldType from, FoldType to,
                   double mix) noexcept TF_NONBLOCKING {
        const double dry = x1_;
        x1_ = x;
        const double u = gain * x + offset;

        if (!primed_) {
            activeTo_ = to;
            activeFrom_ = from;
            u1_ = gain * dry + offset;
            u2_ = u1_;
            offset1_ = offset;
            offset2_ = offset;
            primeModel(to, u1_, u2_);
            if (mix < 1.0 && from != to) primeModel(from, u1_, u2_);
            primed_ = true;
        } else {
            if (to != activeTo_) {
                if (to != activeFrom_) primeModel(to, u1_, u2_);
                activeTo_ = to;
            }
            if (mix < 1.0 && from != activeFrom_) {
                if (from != activeTo_) primeModel(from, u1_, u2_);
                activeFrom_ = from;
            }
        }

        const double offsetCentroid = (offset + offset1_ + offset2_) * (1.0 / 3.0);
        double wet = tickModel(to, u) - shape(to, offsetCentroid);
        if (mix < 1.0 && from != to) {
            const double wetFrom = tickModel(from, u) - shape(from, offsetCentroid);
            wet = mix * wet + (1.0 - mix) * wetFrom;
        } else {
            activeFrom_ = activeTo_;
        }

        u2_ = u1_;
        u1_ = u;
        offset2_ = offset1_;
        offset1_ = offset;

        wet = dc_.process(wet);
        return (1.0 - warp) * dry + warp * wet;
    }

private:
    void primeModel(FoldType type, double u1, double u2) noexcept TF_NONBLOCKING {
        switch (type) {
            case FoldType::fold: fold_.prime(u1, u2); return;
            case FoldType::sine: sine_.prime(u1, u2); return;
            case FoldType::tube: tube_.prime(u1, u2); return;
            case FoldType::buchla: buchla_.prime(u1, u2); return;
            case FoldType::clip: clip_.prime(u1, u2); return;
        }
    }

    double tickModel(FoldType type, double u) noexcept TF_NONBLOCKING {
        switch (type) {
            case FoldType::fold: return fold_.process(u);
            case FoldType::sine: return sine_.process(u);
            case FoldType::tube: return tube_.process(u);
            case FoldType::buchla: return buchla_.process(u);
            case FoldType::clip: return clip_.process(u);
        }
        return u;
    }

    DcBlocker dc_;
    hba::dsp::Adaa2<hba::dsp::TriangleFoldShaper> fold_ {};
    hba::dsp::Adaa2<hba::dsp::SineFoldShaper> sine_ {};
    hba::dsp::Adaa2<hba::dsp::TanhShaper> tube_ {};
    hba::dsp::Adaa2<hba::dsp::Buchla259Shaper> buchla_ {};
    hba::dsp::Adaa2<hba::dsp::HardClipShaper> clip_ {};

    double x1_ = 0.0;
    double u1_ = 0.0;
    double u2_ = 0.0;
    double offset1_ = 0.0;
    double offset2_ = 0.0;
    FoldType activeTo_ = FoldType::fold;
    FoldType activeFrom_ = FoldType::fold;
    bool primed_ = false;
};

}
