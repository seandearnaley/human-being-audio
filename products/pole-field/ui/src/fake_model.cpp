#include "pf/ui/fake_model.h"

#include <cmath>

namespace pf::ui {

FakeModel::FakeModel() {
    visuals_.ppq = 2.6;
    visuals_.peakDbLeft = -4.2f;
    visuals_.peakDbRight = -4.6f;
    visuals_.gainReductionDb = 1.8f;
    visuals_.correlation = 0.84f;
    visuals_.effMorphX = params_[ParamId::morphX] + 0.14f;
    visuals_.effMorphY = params_[ParamId::morphY] + 0.08f;
    visuals_.effMorphZ = params_[ParamId::morphZ];
    visuals_.effFreq = params_[ParamId::freq];
    visuals_.effRes = params_[ParamId::res];
    visuals_.effTransform = params_[ParamId::transform];
    visuals_.effSpread = params_[ParamId::spread];

    for (std::size_t i = 0; i < Visuals::kSpectrumBands; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(Visuals::kSpectrumBands - 1);
        visuals_.spectrumDb[i] = -24.0f - 36.0f * t + 9.0f * std::sin(t * 21.0f) * (1.0f - 0.5f * t);
    }
    for (std::size_t i = 0; i < Visuals::kGonioPoints; ++i) {
        const float a = static_cast<float>(i) * 0.18f;
        visuals_.gonioLeft[i] = 0.55f * std::sin(a) + 0.18f * std::sin(a * 3.1f);
        visuals_.gonioRight[i] = 0.55f * std::sin(a + 0.25f) + 0.18f * std::cos(a * 2.7f);
    }
}

}
