// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/fake_model.h"

#include <hba/dsp/lane_math.h>

namespace cl::ui {

FakeModel::FakeModel() {
    params_[ParamId::lane1Shape] = static_cast<float>(LaneShape::custom);
    params_[ParamId::lane2Shape] = static_cast<float>(LaneShape::custom);
    // Lock Span 0 in the showcase snapshot so the visual lock indicator reads clearly.
    overrides_.spans[0].locked = true;
    overrides_.spans[0].lockedSeed = 407;
    recompile();
}

void FakeModel::set(ParamId id, float real) {
    params_[id] = clampToRange(spec(id), real);
    recompile();
}

void FakeModel::rollSeed() {
    const auto cur = static_cast<std::uint32_t>(params_.integer(ParamId::seed));
    const int next = 1 + static_cast<int>(hba::dsp::laneHash(cur * 2654435761U + 17U) % 999U);
    if (seedCursor_ + 1 < seedHistory_.size())
        seedHistory_.resize(seedCursor_ + 1);
    seedHistory_.push_back(next);
    seedCursor_ = seedHistory_.size() - 1;
    set(ParamId::seed, static_cast<float>(next));
}

bool FakeModel::canStepSeedHistory(int delta) const {
    if (delta < 0)
        return seedCursor_ > 0;
    if (delta > 0)
        return seedCursor_ + 1 < seedHistory_.size();
    return false;
}

void FakeModel::stepSeedHistory(int delta) {
    if (!canStepSeedHistory(delta))
        return;
    seedCursor_ = static_cast<std::size_t>(static_cast<int>(seedCursor_) + (delta < 0 ? -1 : 1));
    set(ParamId::seed, static_cast<float>(seedHistory_[seedCursor_]));
}

void FakeModel::recompile() {
    HeldNotes held;
    compilePhrase(params_, 0, held, visuals_.plan, overrides_);
    visuals_.bpm = 172.0;
    visuals_.phraseIndex = 0;
    visuals_.playheadBeats = 5.25;
    visuals_.ppq = 5.25;
    visuals_.playing = true;
    visuals_.lastBlockEvents = 28;
    visuals_.activityMask = 0x1Fu; // All 5 live MIDI LEDs lit in showcase screenshot
    for (int l = 0; l < kLaneCount; ++l) {
        const auto idx = static_cast<std::size_t>(l);
        visuals_.lanePhase[idx] = static_cast<float>(
            lanePhase(visuals_.ppq, laneLengthQuarters(params_, l), params_[laneParam(l, LaneField::phase)]));
        visuals_.laneValue[idx] = static_cast<float>(laneSend(params_, curves_, l, visuals_.ppq));
    }
}

const Visuals& FakeModel::visuals() { return visuals_; }

} // namespace cl::ui
