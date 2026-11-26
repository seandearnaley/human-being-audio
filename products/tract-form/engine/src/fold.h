// The fold: x' = g x + 0.5 asym through the studio-shared ADAA2 multi-shaper
// (hba::dsp::MultiShaperAdaa2).
#pragma once

#include "tf/nonblocking.h"
#include "tf/params.h"

#include <hba/dsp/adaa.h>
#include <hba/dsp/control.h>

#include <cmath>

namespace tf::dsp {

inline hba::dsp::ShaperModel toShaperModel(FoldType type) noexcept TF_NONBLOCKING {
    switch (type) {
    case FoldType::fold:
        return hba::dsp::ShaperModel::fold;
    case FoldType::sine:
        return hba::dsp::ShaperModel::sine;
    case FoldType::tube:
        return hba::dsp::ShaperModel::tube;
    case FoldType::buchla:
        return hba::dsp::ShaperModel::buchla;
    case FoldType::clip:
        return hba::dsp::ShaperModel::clip;
    }
    return hba::dsp::ShaperModel::fold;
}

inline double shape(FoldType type, double u) noexcept TF_NONBLOCKING {
    return hba::dsp::evalShaper(toShaperModel(type), u);
}

inline double shapeAntiderivative(FoldType type, double u) noexcept TF_NONBLOCKING {
    return hba::dsp::ad1Shaper(toShaperModel(type), u);
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
        shaper_.reset();
    }

    // `mix` crossfades from shaper `from` (0) to `to` (1) when the type changes.
    double process(double x, double gain, double offset, double warp, FoldType from, FoldType to,
                   double mix) noexcept TF_NONBLOCKING {
        double dry = 0.0;
        const double wet =
            dc_.process(shaper_.process(x, gain, offset, toShaperModel(from), toShaperModel(to), mix, dry));
        return (1.0 - warp) * dry + warp * wet;
    }

  private:
    hba::dsp::DcBlocker dc_;
    hba::dsp::MultiShaperAdaa2 shaper_{};
};

}
