// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dt/ui/fake_model.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dt::ui {

FakeModel::FakeModel() {
    presets_ = {
        {"Neuro Core 52", "Neurofunk"},
        {"Sub-Zero Spine", "Neurofunk"},
        {"Twin-T 808 Sub", "Analog Twin-T"},
        {"Membrane Punch", "Techno & Industrial"},
    };
    rebuildPreview();
}

float FakeModel::get(ParamId id) const { return params_[id]; }
void FakeModel::begin(ParamId) {}
void FakeModel::end(ParamId) {}

void FakeModel::set(ParamId id, float real) {
    params_.set(id, real);
    previewDirty_ = true;
}

void FakeModel::setPitchCurve(const hba::dsp::EnvelopeCurve& curve) {
    pitchCurve_ = curve;
    hba::dsp::sanitizeEnvelope(pitchCurve_);
    previewDirty_ = true;
}

void FakeModel::setAmpCurve(const hba::dsp::EnvelopeCurve& curve) {
    ampCurve_ = curve;
    hba::dsp::sanitizeEnvelope(ampCurve_);
    previewDirty_ = true;
}

void FakeModel::setPattern(const ComposePattern& p) {
    pattern_ = p;
    sanitizePattern(pattern_);
    previewDirty_ = true;
}

void FakeModel::triggerAudition(int slotIndex, float) {
    if (slotIndex >= 0 && slotIndex < kKitSlots)
        params_.set(ParamId::activeSlot, static_cast<float>(slotIndex));
    telemetry_.sounding = true;
    telemetry_.activeVoices = 1;
    previewDirty_ = true;
}

void FakeModel::applyArchetype(int archetypeIndex) {
    params_ = Params::defaults();
    pitchCurve_ = hba::dsp::defaultKickPitchCurve();
    ampCurve_ = hba::dsp::defaultKickAmpCurve();
    switch (archetypeIndex) {
    case 0: // Neuro
        params_.set(ParamId::coreTopology, 2.0f);
        params_.set(ParamId::tuneHz, 52.0f);
        params_.set(ParamId::lengthMs, 138.0f);
        params_.set(ParamId::punchAmt, 0.72f);
        params_.set(ParamId::clickAmt, 0.56f);
        params_.set(ParamId::drive, 0.44f);
        params_.set(ParamId::tight, 0.42f);
        presetName_ = "Neuro Core 52";
        break;
    case 1: // 808
        params_.set(ParamId::coreTopology, 1.0f);
        params_.set(ParamId::tuneHz, 44.0f);
        params_.set(ParamId::lengthMs, 420.0f);
        params_.set(ParamId::punchAmt, 0.36f);
        params_.set(ParamId::clickAmt, 0.16f);
        params_.set(ParamId::drive, 0.28f);
        params_.set(ParamId::tight, 0.12f);
        presetName_ = "Twin-T 808";
        break;
    case 2: // 909
        params_.set(ParamId::coreTopology, 1.0f);
        params_.set(ParamId::tuneHz, 55.0f);
        params_.set(ParamId::lengthMs, 195.0f);
        params_.set(ParamId::punchAmt, 0.78f);
        params_.set(ParamId::clickAmt, 0.62f);
        params_.set(ParamId::drive, 0.42f);
        params_.set(ParamId::tight, 0.28f);
        presetName_ = "Analog 909";
        break;
    case 3: // Tech
        params_.set(ParamId::coreTopology, 3.0f);
        params_.set(ParamId::tuneHz, 49.0f);
        params_.set(ParamId::lengthMs, 180.0f);
        params_.set(ParamId::punchAmt, 0.66f);
        params_.set(ParamId::clickAmt, 0.54f);
        params_.set(ParamId::drive, 0.62f);
        params_.set(ParamId::tight, 0.32f);
        params_.set(ParamId::tension, 0.45f);
        presetName_ = "Tech Membrane";
        break;
    case 4: // Half
        params_.set(ParamId::coreTopology, 0.0f);
        params_.set(ParamId::tuneHz, 41.2f);
        params_.set(ParamId::lengthMs, 260.0f);
        params_.set(ParamId::punchAmt, 0.52f);
        params_.set(ParamId::clickAmt, 0.42f);
        params_.set(ParamId::drive, 0.34f);
        params_.set(ParamId::tight, 0.18f);
        presetName_ = "Halftime 41";
        break;
    case 5: // Sub
        params_.set(ParamId::coreTopology, 0.0f);
        params_.set(ParamId::tuneHz, 46.2f);
        params_.set(ParamId::lengthMs, 175.0f);
        params_.set(ParamId::punchAmt, 0.45f);
        params_.set(ParamId::clickAmt, 0.18f);
        params_.set(ParamId::drive, 0.14f);
        params_.set(ParamId::tight, 0.25f);
        presetName_ = "Pure Sub 46";
        break;
    case 6: // Click
        params_.set(ParamId::coreTopology, 2.0f);
        params_.set(ParamId::tuneHz, 58.0f);
        params_.set(ParamId::lengthMs, 112.0f);
        params_.set(ParamId::punchAmt, 0.80f);
        params_.set(ParamId::clickAmt, 0.82f);
        params_.set(ParamId::drive, 0.50f);
        params_.set(ParamId::tight, 0.62f);
        presetName_ = "Scalpel Snap";
        break;
    default:
        break;
    }
    previewDirty_ = true;
}

void FakeModel::mutateKick() {
    auto nextFloat = [this](float lo, float hi) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const float u = static_cast<float>(rng_ >> 8) / static_cast<float>(1u << 24);
        return lo + u * (hi - lo);
    };
    params_.set(ParamId::punchAmt, std::clamp(params_[ParamId::punchAmt] + nextFloat(-0.18f, 0.18f), 0.15f, 0.95f));
    params_.set(ParamId::clickAmt, std::clamp(params_[ParamId::clickAmt] + nextFloat(-0.22f, 0.22f), 0.10f, 0.92f));
    params_.set(ParamId::drive, std::clamp(params_[ParamId::drive] + nextFloat(-0.18f, 0.18f), 0.05f, 0.82f));
    params_.set(ParamId::harm2, nextFloat(0.10f, 0.55f));
    params_.set(ParamId::harm3, nextFloat(0.05f, 0.40f));
    params_.set(ParamId::fmRatio, nextFloat(1.3f, 4.2f));
    params_.set(ParamId::beaterTone, nextFloat(1200.0f, 6800.0f));
    previewDirty_ = true;
}

void FakeModel::matchSubLinkPhase() {
    if (!telemetry_.scActive)
        return;
    float bestCorr = 0.0f;
    const float bestDeg =
        Engine::solveSubLinkPhase(params_, pitchCurve_, ampCurve_, pattern_, telemetry_.scScope, &bestCorr);
    params_.set(ParamId::startPhase, bestDeg);
    telemetry_.scCorrelation = bestCorr;
    previewDirty_ = true;
}

void FakeModel::tuneToSubLinkRoot() {
    if (telemetry_.scActive && telemetry_.scRootHz >= 28.0f && telemetry_.scRootHz <= 120.0f) {
        params_.set(ParamId::tuneHz, telemetry_.scRootHz);
        previewDirty_ = true;
    }
}

void FakeModel::loadPreset(int index) {
    if (presets_.empty())
        return;
    presetIndex_ = std::clamp(index, 0, static_cast<int>(presets_.size()) - 1);
    applyArchetype(presetIndex_);
    presetName_ = presets_[static_cast<std::size_t>(presetIndex_)].name;
}

void FakeModel::stepPreset(int delta) {
    if (presets_.empty())
        return;
    const int n = static_cast<int>(presets_.size());
    loadPreset((presetIndex_ + delta + n) % n);
}

void FakeModel::setDemoSidechain(bool on) {
    demoSidechain_ = on;
    previewDirty_ = true;
}

void FakeModel::refreshVisuals() {
    if (previewDirty_)
        rebuildPreview();
}

const TelemetrySnapshot& FakeModel::visuals() {
    if (previewDirty_)
        rebuildPreview();
    return telemetry_;
}

const PreviewSnapshot& FakeModel::preview() {
    if (previewDirty_)
        rebuildPreview();
    return preview_;
}

void FakeModel::rebuildPreview() {
    const int slot = params_.choice(ParamId::activeSlot);
    preview_ = Engine::renderPreview(params_, pitchCurve_, ampCurve_, pattern_, nullptr, slot, 48000.0);
    telemetry_.peakL = 0.88f;
    telemetry_.peakR = 0.86f;
    telemetry_.truePeakDb = preview_.truePeakDb;
    telemetry_.rmsDb = preview_.rmsDb;
    telemetry_.crestDb = preview_.crestDb;
    telemetry_.dcPercent = preview_.dcPercent;
    telemetry_.limiterGrDb = 0.6f;
    telemetry_.activeVoices = 1;
    telemetry_.lastSlot = slot;
    telemetry_.kickScope = preview_.sumWave;
    if (demoSidechain_) {
        telemetry_.scActive = true;
        telemetry_.scRootHz = params_[ParamId::tuneHz];
        const float w = 2.0f * std::numbers::pi_v<float> * telemetry_.scRootHz * 0.180f;
        for (int i = 0; i < kScopePoints; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(kScopePoints - 1);
            const float sc = 0.62f * std::sin(w * u + 0.35f);
            telemetry_.scScope[static_cast<std::size_t>(i)] = sc;
            telemetry_.sumScope[static_cast<std::size_t>(i)] =
                0.5f * (preview_.sumWave[static_cast<std::size_t>(i)] + sc);
        }
        telemetry_.scCorrelation = 0.91f;
        telemetry_.scPeakDeltaDb = -0.2f;
    } else {
        telemetry_.scActive = false;
        telemetry_.scCorrelation = 0.0f;
        telemetry_.scRootHz = 0.0f;
        telemetry_.sumScope = preview_.sumWave;
    }
    previewDirty_ = false;
}

} // namespace dt::ui
