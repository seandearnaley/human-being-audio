// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ln/ui/fake_model.h"

#include <cmath>

namespace ln::ui {

FakeModel::FakeModel() {
    visuals_.peakL = 0.62f;
    visuals_.peakR = 0.58f;
    visuals_.rms = 0.44f;
    visuals_.subPeak = 0.52f;
    visuals_.limiterGain = 0.92f;
    visuals_.effFilt1Hz = params_[ParamId::filt1Hz];
    visuals_.effFilt2Hz = params_[ParamId::filt2Hz];
    visuals_.effDrive = params_[ParamId::drive];
    visuals_.effTone = params_[ParamId::tone];
    visuals_.effGenMix = 0.55f;
    visuals_.effStrataPos = 0.14f;
    visuals_.effStrataSpan = 0.72f;
    visuals_.effDisperse = params_[ParamId::disperse];
    visuals_.effDensity = params_[ParamId::density];
    visuals_.effWidth = params_[ParamId::width];
    visuals_.effSubWarmth = params_[ParamId::subWarmth];
    visuals_.playheadNorm = 0.36f;
    visuals_.loopStartNorm = 0.14f;
    visuals_.loopEndNorm = 0.86f;
    visuals_.activeNote = 29;
    visuals_.generation = 2;
    visuals_.sounding = true;
    visuals_.hasPrint = true;
    visuals_.ppq = 2.4;
    visuals_.notePpq = 1.2;
    visuals_.freeSec = 1.45;
    visuals_.noteSec = 0.62;

    for (std::size_t i = 0; i < TelemetrySnapshot::kScopePoints; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(TelemetrySnapshot::kScopePoints - 1);
        visuals_.scope[i] =
            0.52f * std::sin(t * 12.566f) + 0.24f * std::sin(t * 31.4f + 0.7f) + 0.12f * std::cos(t * 50.2f);
    }
    for (std::size_t v = 0; v < 7; ++v)
        visuals_.voicePhases[v] = std::fmod(0.17f * static_cast<float>(v) + 0.31f, 1.0f);

    for (std::size_t i = 0; i < PrintBuffer::kWaveformPoints; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(PrintBuffer::kWaveformPoints - 1);
        const float swell = 0.45f + 0.45f * std::sin(t * 9.424f) * std::sin(t * 9.424f);
        const float grit = 0.16f * std::abs(std::sin(t * 53.0f + 0.4f));
        peaks_[i] = std::clamp(swell * 0.82f + grit, 0.08f, 0.96f);
    }
}

} // namespace ln::ui
