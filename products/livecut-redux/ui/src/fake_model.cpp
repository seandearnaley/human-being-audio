// SPDX-License-Identifier: GPL-3.0-or-later
#include "lc/ui/fake_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lc::ui {

FakeModel::FakeModel() {
    presets_ = {
        {"Amen Instant", "Pad", true},       {"Triplet Bounce", "Pad", true},
        {"Glitch Crunch", "Pad", true},      {"Suction Breath", "Pad", true},
        {"Resonant Sweep", "Pad", true},     {"Tape Brake Jam", "Pad", true},
        {"Nested Loops", "Pad", true},       {"Barline Rush", "Pad", true},
        {"Jungle Chop 174", "Jungle", true}, {"Squarepusher Fill", "Jungle", true},
        {"Neuro Sub-Lock", "Neuro", true},   {"Reese Fold Crunch", "Neuro", true},
        {"Grain Cloud 8v", "Glitch", true},  {"Harmonic Fifths", "Resonance", true},
    };

    addChoice("procedure", {"CutProc11", "WarpCut", "SQPusher", "Bounce"}, 0);
    addChoice("subdivision", {"6", "8", "12", "16", "18", "24", "32"}, 1);
    addInt("seed", 1, 16, 7);
    addInt("minPhraseBars", 1, 8, 1);
    addInt("maxPhraseBars", 1, 8, 4);
    addFloat("fade", 0.0f, 10.0f, 2.5f, "ms");
    addFloat("minAmp", 0.0f, 1.0f, 0.85f, "%");
    addFloat("maxAmp", 0.0f, 1.0f, 1.0f, "%");
    addFloat("minPan", -1.0f, 1.0f, -0.3f);
    addFloat("maxPan", -1.0f, 1.0f, 0.3f);
    addFloat("minPitch", -2400.0f, 1200.0f, -200.0f, "ct");
    addFloat("maxPitch", -2400.0f, 1200.0f, 200.0f, "ct");
    addFloat("duty", 0.1f, 1.0f, 0.9f, "%");
    addFloat("fillDuty", 0.1f, 1.0f, 0.75f, "%");
    addInt("minRepeats", 0, 4, 1);
    addInt("maxRepeats", 0, 4, 3);
    addFloat("stutterChance", 0.0f, 1.0f, 0.75f, "%");
    addFloat("stutterArea", 0.1f, 1.0f, 0.5f, "%");
    addFloat("straightChance", 0.0f, 1.0f, 0.3f, "%");
    addFloat("regularChance", 0.0f, 1.0f, 0.5f, "%");
    addFloat("ritardChance", 0.0f, 1.0f, 0.5f, "%");
    addFloat("accel", 0.5f, 0.99f, 0.9f);
    addFloat("activity", 0.0f, 1.0f, 0.6f, "%");
    addFloat("bounceChance", 0.0f, 1.0f, 0.45f, "%");
    addFloat("ratchetChance", 0.0f, 1.0f, 0.7f, "%");
    addInt("climb", -12, 24, 7, "st");
    addFloat("halftimeChance", 0.0f, 1.0f, 0.15f, "%");
    addBool("crusher", true);
    addFloat("minBits", 1.0f, 32.0f, 8.0f, "bit");
    addFloat("maxBits", 1.0f, 32.0f, 16.0f, "bit");
    addFloat("minRate", 100.0f, 192000.0f, 6000.0f, "Hz");
    addFloat("maxRate", 100.0f, 192000.0f, 24000.0f, "Hz");
    addBool("comb", false);
    addChoice("combType", {"Feed-forward", "Feedback"}, 1);
    addFloat("combFeedback", 0.0f, 0.95f, 0.55f, "%");
    addFloat("minDelay", 1.0f, 50.0f, 6.0f, "ms");
    addFloat("maxDelay", 1.0f, 50.0f, 22.0f, "ms");
    addFloat("wet", 0.0f, 1.0f, 1.0f, "%");
    addFloat("outputTrim", -24.0f, 24.0f, 0.0f, "dB");
    addFloat("swing", 50.0f, 75.0f, 54.0f, "%");
    addFloat("accent", 0.0f, 1.0f, 0.25f, "%");
    addBool("hold", false);
    addChoice("mode", {"Live", "Studio"}, 0);
    addBool("attackAlign", true);
    addFloat("attackThreshold", 3.0f, 24.0f, 6.0f, "dB");
    addFloat("reverseChance", 0.0f, 1.0f, 0.25f, "%");
    addBool("impactFilterEnabled", true);
    addChoice("impactFilterType", {"Low-pass", "Band-pass", "High-pass"}, 0);
    addFloat("impactFilterChance", 0.0f, 1.0f, 0.8f, "%");
    addFloat("impactFilterCutoff", 30.0f, 18000.0f, 2400.0f, "Hz");
    addFloat("impactFilterQ", 0.5f, 8.0f, 2.2f);
    addFloat("impactFilterEnvOctaves", -6.0f, 6.0f, 2.4f, "oct");
    addFloat("impactFilterRepeatOctaves", -4.0f, 4.0f, 1.0f, "oct");
    addFloat("impactFilterMix", 0.0f, 1.0f, 0.85f, "%");
    addBool("impactDriveEnabled", true);
    addChoice("impactDriveMode", {"Soft Drive", "Wavefold"}, 0);
    addFloat("impactDriveChance", 0.0f, 1.0f, 0.7f, "%");
    addFloat("impactDriveDb", 0.0f, 30.0f, 9.0f, "dB");
    addFloat("impactDriveEnvDb", -24.0f, 24.0f, 6.0f, "dB");
    addFloat("impactDriveRepeatDb", -24.0f, 24.0f, 3.0f, "dB");
    addFloat("impactDriveMix", 0.0f, 1.0f, 0.65f, "%");
    addBool("impactTransientEnabled", true);
    addFloat("impactTransientChance", 0.0f, 1.0f, 1.0f, "%");
    addFloat("impactTransientAttackDb", -18.0f, 18.0f, 5.0f, "dB");
    addFloat("impactTransientSustainDb", -18.0f, 18.0f, -3.0f, "dB");
    addFloat("impactTransientTimeMs", 10.0f, 200.0f, 35.0f, "ms");
    addFloat("impactTransientMix", 0.0f, 1.0f, 0.8f, "%");
    addFloat("impactAttackMs", 0.0f, 25.0f, 1.5f, "ms");
    addFloat("impactDecayMs", 5.0f, 1000.0f, 110.0f, "ms");
    addChoice("fractureProcedure", {"Classic", "GrainCut", "TapeCut"}, 0);
    addFloat("fractureActivity", 0.0f, 1.0f, 0.6f, "%");
    addFloat("fractureGrainSizeMs", 10.0f, 160.0f, 48.0f, "ms");
    addFloat("fractureGrainDensity", 0.5f, 4.0f, 2.2f);
    addFloat("fractureGrainPositionSpreadMs", 0.0f, 250.0f, 65.0f, "ms");
    addFloat("fractureGrainPitchSpreadCents", 0.0f, 1200.0f, 350.0f, "ct");
    addFloat("fractureGrainPanSpread", 0.0f, 1.0f, 0.45f, "%");
    addFloat("fractureTapeStartSpeed", 0.0f, 1.0f, 1.0f, "x");
    addFloat("fractureTapeEndSpeed", 0.0f, 1.0f, 0.0f, "x");
    addChoice("fractureTapeCurve", {"Linear", "Smooth"}, 1);
    addChoice("fractureTapeCycle", {"1/2 Beat", "1 Beat", "2 Beats", "4 Beats"}, 1);
    addBool("fractureTapeAlternate", false);
    addBool("fractureFrequencyEnabled", false);
    addChoice("fractureFrequencyMode", {"Ring Mod", "Freq Shift"}, 1);
    addFloat("fractureFrequencyChance", 0.0f, 1.0f, 0.6f, "%");
    addFloat("fractureFrequencyHz", -2000.0f, 2000.0f, 140.0f, "Hz");
    addFloat("fractureFrequencyEnvHz", -2000.0f, 2000.0f, 220.0f, "Hz");
    addFloat("fractureFrequencyRepeatHz", -2000.0f, 2000.0f, 80.0f, "Hz");
    addFloat("fractureFrequencyMix", 0.0f, 1.0f, 0.5f, "%");
    addBool("resonanceEnabled", true);
    addFloat("resonanceRootHz", 40.0f, 800.0f, 110.0f, "Hz");
    addFloat("resonanceDecayMs", 40.0f, 5000.0f, 620.0f, "ms");
    addFloat("resonanceExcitationDb", -12.0f, 12.0f, 2.0f, "dB");
    addInt("resonanceInterval1", -12, 24, 0, "st");
    addInt("resonanceInterval2", -12, 24, 7, "st");
    addInt("resonanceInterval3", -12, 24, 12, "st");
    addInt("resonanceInterval4", -12, 24, 19, "st");
    addFloat("resonanceWeight1", 0.0f, 1.0f, 1.0f, "%");
    addFloat("resonanceWeight2", 0.0f, 1.0f, 0.8f, "%");
    addFloat("resonanceWeight3", 0.0f, 1.0f, 0.7f, "%");
    addFloat("resonanceWeight4", 0.0f, 1.0f, 0.5f, "%");
    addFloat("resonanceMix", 0.0f, 1.0f, 0.45f, "%");
    addChoice("resonanceOrder", {"Impact > Res > Freq", "Res > Impact > Freq", "Impact > Freq > Res"}, 0);
    addBool("resonanceSpectralEnabled", false);
    addBool("resonanceFreeze", false);
    addFloat("resonanceSmearMs", 0.0f, 2000.0f, 280.0f, "ms");
    addFloat("resonanceSpectralMix", 0.0f, 1.0f, 0.75f, "%");
    addChoice("padProgram", {"Cut", "Bounce", "Glitch", "Suction", "Filter", "Tape", "Loops", "Rush"}, 0);
    addFloat("padX", 0.0f, 1.0f, 0.62f, "%");
    addFloat("padY", 0.0f, 1.0f, 0.68f, "%");
    addBool("padTouch", false);
    addBool("padHold", true);
    addChoice("padQuantize", {"Beat", "Bar"}, 0);
    addChoice("surface", {"Pad", "Classic"}, 0);
    addBool("padStraight", false);
    addFloat("subAnchor", 0.0f, 200.0f, 110.0f, "Hz");
    addBool("limiter", true);

    populateDemoTelemetry();
}

void FakeModel::addFloat(std::string id, float min, float max, float def, std::string unit) {
    specs_[std::move(id)] = {min, max, def, def, false, false, std::move(unit), {}};
}

void FakeModel::addInt(std::string id, int min, int max, int def, std::string unit) {
    specs_[std::move(id)] = {static_cast<float>(min),
                             static_cast<float>(max),
                             static_cast<float>(def),
                             static_cast<float>(def),
                             false,
                             true,
                             std::move(unit),
                             {}};
}

void FakeModel::addBool(std::string id, bool def) {
    specs_[std::move(id)] = {0.0f, 1.0f, def ? 1.0f : 0.0f, def ? 1.0f : 0.0f, true, false, {}, {}};
}

void FakeModel::addChoice(std::string id, std::vector<std::string> choices, int def) {
    const float mx = choices.size() > 1 ? static_cast<float>(choices.size() - 1) : 1.0f;
    specs_[std::move(id)] = {0.0f, mx, static_cast<float>(def), static_cast<float>(def), false,
                             true, {}, std::move(choices)};
}

void FakeModel::populateDemoTelemetry() {
    visuals_.cutting = true;
    visuals_.waitingHost = false;
    visuals_.bpm = 172.0;
    visuals_.beatsPerBar = 4.0;
    visuals_.beat = 2.35;
    visuals_.unitsPerBar = 8;
    visuals_.phraseBars = 2;
    visuals_.blockStart = 8.0;
    visuals_.blockEnd = 12.0;
    visuals_.positionInPhrase = 10.4;
    visuals_.padActiveProgram = static_cast<int>(std::lround(getReal("padProgram")));
    visuals_.padEngaged = true;
    visuals_.outputPeakL = 0.78f;
    visuals_.outputPeakR = 0.74f;
    visuals_.limiterGrDb = 0.8f;
    visuals_.hasCapture = true;
    visuals_.captureStatus = "2 bars ready \u00b7 drag to DAW";
    visuals_.headline = "Bar 2 \u00b7 CutProc11 \u00b7 1/8";
    visuals_.holdStatus = "Live stream";
    visuals_.transformStatus = "Attack align ready \u00b7 14 hits snapped";
    visuals_.fractureStatus = "Classic reader; Ring / Shift available";
    visuals_.resonatorStatus = "Resonator tuning ready \u00b7 110 Hz";
    visuals_.spectralStatus = "Active: Live / Spectral off\nEnable Spectral and click Apply while stopped.";

    for (std::size_t i = 0; i < VisualState::kWaveformBins; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(VisualState::kWaveformBins);
        const float beatEnv = std::exp(-5.0f * std::fmod(u * 8.0f, 1.0f));
        const float snare = (i % 16 == 8) ? 0.92f : 0.0f;
        const float ghost = 0.18f + 0.14f * std::abs(std::sin(u * 47.0f));
        visuals_.waveform[i] = std::clamp(0.68f * beatEnv + snare * 0.35f + ghost, 0.04f, 0.98f);
        visuals_.sliceMarks[i] = (i % 16 == 0 || i == 72 || i == 84 || i == 96) ? 1u : 0u;
    }
}

float FakeModel::getNormalised(std::string_view id) const {
    const auto it = specs_.find(std::string(id));
    if (it == specs_.end())
        return 0.0f;
    const auto& s = it->second;
    return s.max > s.min ? std::clamp((s.value - s.min) / (s.max - s.min), 0.0f, 1.0f) : 0.0f;
}

float FakeModel::getReal(std::string_view id) const {
    const auto it = specs_.find(std::string(id));
    return it == specs_.end() ? 0.0f : it->second.value;
}

float FakeModel::defaultNormalised(std::string_view id) const {
    const auto it = specs_.find(std::string(id));
    if (it == specs_.end())
        return 0.0f;
    const auto& s = it->second;
    return s.max > s.min ? std::clamp((s.def - s.min) / (s.max - s.min), 0.0f, 1.0f) : 0.0f;
}

void FakeModel::beginGesture(std::string_view) {}

void FakeModel::setNormalised(std::string_view id, float normalised) {
    auto it = specs_.find(std::string(id));
    if (it == specs_.end())
        return;
    auto& s = it->second;
    const float n = std::clamp(normalised, 0.0f, 1.0f);
    s.value = s.min + n * (s.max - s.min);
    if (s.isBool)
        s.value = n >= 0.5f ? 1.0f : 0.0f;
    else if (s.isInt)
        s.value = std::round(s.value);
    if (id == "padProgram")
        visuals_.padActiveProgram = static_cast<int>(std::lround(s.value));
}

void FakeModel::endGesture(std::string_view) { ++undoDepth_; }

void FakeModel::setReal(std::string_view id, float value) {
    auto it = specs_.find(std::string(id));
    if (it == specs_.end())
        return;
    it->second.value = std::clamp(value, it->second.min, it->second.max);
    if (id == "padProgram")
        visuals_.padActiveProgram = static_cast<int>(std::lround(it->second.value));
}

std::string FakeModel::formatValue(std::string_view id) const {
    const auto it = specs_.find(std::string(id));
    if (it == specs_.end())
        return {};
    const auto& s = it->second;
    if (!s.choices.empty()) {
        const int idx = std::clamp(static_cast<int>(std::lround(s.value)), 0, static_cast<int>(s.choices.size()) - 1);
        return s.choices[static_cast<std::size_t>(idx)];
    }
    if (s.isBool)
        return s.value >= 0.5f ? "On" : "Off";
    if (id == "subAnchor" && s.value < 1.0f)
        return "Off";
    char buf[32];
    if (s.unit == "%") {
        const float pct = s.max <= 1.0f ? s.value * 100.0f : s.value;
        std::snprintf(buf, sizeof(buf), "%.0f%%", pct);
    } else if (s.isInt) {
        if (s.unit.empty())
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(s.value)));
        else
            std::snprintf(buf, sizeof(buf), "%d %s", static_cast<int>(std::lround(s.value)), s.unit.c_str());
    } else if (s.unit == "Hz" && std::abs(s.value) >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.1f kHz", s.value / 1000.0f);
    } else if (s.unit.empty()) {
        std::snprintf(buf, sizeof(buf), "%.2f", s.value);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f %s", s.value, s.unit.c_str());
    }
    return buf;
}

std::vector<std::string> FakeModel::choices(std::string_view id) const {
    const auto it = specs_.find(std::string(id));
    return it == specs_.end() ? std::vector<std::string>{} : it->second.choices;
}

bool FakeModel::isBool(std::string_view id) const {
    const auto it = specs_.find(std::string(id));
    return it != specs_.end() && it->second.isBool;
}

void FakeModel::loadPreset(int index) {
    if (presets_.empty())
        return;
    presetIndex_ = std::clamp(index, 0, static_cast<int>(presets_.size()) - 1);
    currentPreset_ = presets_[static_cast<std::size_t>(presetIndex_)].name;
}

void FakeModel::stepPreset(int delta) {
    if (presets_.empty())
        return;
    const int n = static_cast<int>(presets_.size());
    presetIndex_ = ((presetIndex_ + delta) % n + n) % n;
    currentPreset_ = presets_[static_cast<std::size_t>(presetIndex_)].name;
}

void FakeModel::undo() {
    if (undoDepth_ > 0) {
        --undoDepth_;
        ++redoDepth_;
    }
}

void FakeModel::redo() {
    if (redoDepth_ > 0) {
        --redoDepth_;
        ++undoDepth_;
    }
}

void FakeModel::rollSeed() {
    const int cur = static_cast<int>(std::lround(getReal("seed")));
    const int next = (cur % 16) + 1;
    setReal("seed", static_cast<float>(next));
}

void FakeModel::requestCapture() {
    visuals_.hasCapture = true;
    visuals_.captureStatus = "Captured 2 bars \u00b7 drag to DAW";
}

void FakeModel::applyRequestedMode() {
    visuals_.activeStudio = getNormalised("mode") >= 0.5f;
    visuals_.activeSpectral = getNormalised("resonanceSpectralEnabled") >= 0.5f;
    visuals_.canApplyMode = false;
}

void FakeModel::refreshVisuals() {
    visuals_.padActiveProgram = static_cast<int>(std::lround(getReal("padProgram")));
    const bool reqStudio = getNormalised("mode") >= 0.5f;
    const bool reqSpec = getNormalised("resonanceSpectralEnabled") >= 0.5f;
    visuals_.canApplyMode = (reqStudio != visuals_.activeStudio) || (reqSpec != visuals_.activeSpectral);
}

} // namespace lc::ui
