// Second-order antiderivative anti-aliased (ADAA2) waveshaper for pre-, inter-,
// and post-lattice saturation, backed by hba::dsp::MultiShaperAdaa2.
#pragma once

#include "control.h"
#include "pf/params.h"

#include <hba/dsp/adaa.h>

#include <algorithm>

namespace pf::dsp {

inline hba::dsp::ShaperModel toShaperModel(FoldType type) noexcept PF_NONBLOCKING {
    switch (type) {
    case FoldType::tube:
        return hba::dsp::ShaperModel::tube;
    case FoldType::clip:
        return hba::dsp::ShaperModel::clip;
    case FoldType::fold:
        return hba::dsp::ShaperModel::fold;
    case FoldType::buchla:
        return hba::dsp::ShaperModel::buchla;
    case FoldType::sine:
        return hba::dsp::ShaperModel::sine;
    }
    return hba::dsp::ShaperModel::tube;
}

class Saturator {
public:
    void prepare(double sampleRate) noexcept PF_NONBLOCKING {
        dc_.prepare(10.0, sampleRate);
        xfadeInc_ = 1.0 / std::max(1.0, 0.005 * sampleRate);  // 5 ms click-free shaper crossfade
        reset();
    }

    void reset() noexcept PF_NONBLOCKING {
        dc_.reset();
        shaper_.reset();
        active_ = FoldType::tube;
        prev_ = FoldType::tube;
        xfade_ = 1.0;
        primed_ = false;
    }

    double process(double x, double drive01, double asym01, FoldType type) noexcept PF_NONBLOCKING {
        if (drive01 <= 1.0e-5) {
            active_ = type;
            prev_ = type;
            xfade_ = 1.0;
            primed_ = false;
            dc_.reset();
            return shaper_.bypass(x);
        }

        if (!primed_) {
            active_ = type;
            prev_ = type;
            xfade_ = 1.0;
            primed_ = true;
        } else if (type != active_) {
            prev_ = active_;
            active_ = type;
            xfade_ = 0.0;
        }

        const double gain = dbToGain(drive01 * 24.0);
        const double offset = 0.45 * asym01 * drive01;
        double dry = 0.0;
        const double shaped =
            shaper_.process(x, gain, offset, toShaperModel(prev_), toShaperModel(active_), xfade_, dry);
        if (xfade_ < 1.0)
            xfade_ = std::min(1.0, xfade_ + xfadeInc_);

        const double wet = dc_.process(shaped);
        const double blend = std::min(1.0, drive01 * 4.0);
        const double comp = 1.0 / (1.0 + 0.35 * drive01);
        return (1.0 - blend) * dry + blend * wet * comp;
    }

  private:
    DcBlocker dc_;
    hba::dsp::MultiShaperAdaa2 shaper_{};
    double xfade_ = 1.0;
    double xfadeInc_ = 1.0 / 960.0;
    FoldType active_ = FoldType::tube;
    FoldType prev_ = FoldType::tube;
    bool primed_ = false;
};

}
