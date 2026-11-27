// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pv/ui/fake_model.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace pv::ui {

FakeModel::FakeModel() {
    engine_.prepare(48000.0, 256);
    warmUpEngine(false, false);
}

void FakeModel::set(ParamId id, float real) {
    params_.set(id, real);
    engine_.setParams(params_);
}

void FakeModel::setChordMask(std::uint16_t mask) {
    const auto clean = static_cast<std::uint16_t>(mask & 0x0FFFu);
    chordMask_ = clean != 0u ? clean : kDefaultChordMask;
    engine_.setChordMask(chordMask_);
}

hba::dsp::Curve FakeModel::laneCurve(int lane) const {
    return curves_[static_cast<std::size_t>(std::clamp(lane, 0, kLaneCount - 1))];
}

void FakeModel::setLaneCurve(int lane, const hba::dsp::Curve& curve) {
    if (lane >= 0 && lane < kLaneCount) {
        curves_[static_cast<std::size_t>(lane)] = curve;
        engine_.setLaneCurves(curves_);
    }
}

void FakeModel::warmUpEngine(bool simulateSidechain, bool simulateMidi) {
    Params runParams = params_;
    runParams.set(ParamId::demoAudition, 1.0f);
    engine_.setParams(runParams);
    engine_.setChordMask(chordMask_);
    engine_.setLaneCurves(curves_);
    engine_.allNotesOff();
    if (simulateMidi) {
        engine_.noteOn(53, 0.92f);
        engine_.noteOn(56, 0.85f);
        engine_.noteOn(60, 0.90f);
        engine_.noteOn(63, 0.84f);
        engine_.noteOn(67, 0.88f);
    }

    constexpr int kBlock = 256;
    std::vector<float> zero(kBlock, 0.0f), scL(kBlock, 0.0f), scR(kBlock, 0.0f), outL(kBlock, 0.0f), outR(kBlock, 0.0f);
    Transport tr{172.0, 0.0, true};
    for (int blk = 0; blk < 96; ++blk) {
        tr.ppq = (static_cast<double>(blk * kBlock) / 48000.0) * (172.0 / 60.0);
        engine_.setTransport(tr);
        if (simulateSidechain) {
            for (int i = 0; i < kBlock; ++i) {
                const double t = static_cast<double>(blk * kBlock + i) / 48000.0;
                scL[static_cast<std::size_t>(i)] = static_cast<float>(0.35 * std::sin(hba::dsp::kTwoPi * 349.23 * t));
                scR[static_cast<std::size_t>(i)] = static_cast<float>(0.35 * std::sin(hba::dsp::kTwoPi * 523.25 * t));
            }
        }
        engine_.process(zero.data(), zero.data(), simulateSidechain ? scL.data() : nullptr,
                        simulateSidechain ? scR.data() : nullptr, outL.data(), outR.data(), kBlock);
    }
    telemetry_ = engine_.telemetry();
}

const TelemetrySnapshot& FakeModel::visuals() { return telemetry_; }

std::vector<PresetEntry> FakeModel::presets() const {
    return {
        {"Color Bass", "Iridescent F Min9"},
        {"Neurofunk", "Phrygian Razor Comb"},
        {"Future Bass", "Eb Maj9 Crystal Pad"},
        {"Carrier & Freeze", "Glass Halo Freeze"},
    };
}

void FakeModel::loadPreset(int index) {
    const auto list = presets();
    if (index >= 0 && index < static_cast<int>(list.size()))
        presetName_ = list[static_cast<std::size_t>(index)].name;
}

void FakeModel::stepPreset(int delta) {
    const auto list = presets();
    if (list.empty())
        return;
    int cur = 0;
    for (int i = 0; i < static_cast<int>(list.size()); ++i)
        if (list[static_cast<std::size_t>(i)].name == presetName_)
            cur = i;
    const int n = static_cast<int>(list.size());
    loadPreset((cur + delta % n + n) % n);
}

} // namespace pv::ui
