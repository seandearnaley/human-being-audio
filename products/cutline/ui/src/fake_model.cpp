// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/fake_model.h"

#include <hba/dsp/lane_math.h>

namespace cl::ui {

FakeModel::FakeModel() {
    params_[ParamId::lane1Shape] = static_cast<float>(LaneShape::custom);
    params_[ParamId::lane2Shape] = static_cast<float>(LaneShape::custom);
    recompile();
}

void FakeModel::set(ParamId id, float real) {
    params_[id] = clampToRange(spec(id), real);
    recompile();
}

void FakeModel::rollSeed() {
    const auto cur = static_cast<std::uint32_t>(params_.integer(ParamId::seed));
    const int next = 1 + static_cast<int>(hba::dsp::laneHash(cur * 2654435761U + 17U) % 999U);
    set(ParamId::seed, static_cast<float>(next));
}

void FakeModel::recompile() {
    HeldNotes held;
    compilePhrase(params_, 0, held, visuals_.plan);
    visuals_.playheadBeats = 5.25;
    visuals_.ppq = 5.25;
    visuals_.playing = true;
    visuals_.lastBlockEvents = 28;
    for (int l = 0; l < kLaneCount; ++l) {
        const auto idx = static_cast<std::size_t>(l);
        visuals_.lanePhase[idx] = static_cast<float>(
            lanePhase(visuals_.ppq, laneLengthQuarters(params_, l), params_[laneParam(l, LaneField::phase)]));
        visuals_.laneValue[idx] = static_cast<float>(laneSend(params_, curves_, l, visuals_.ppq));
    }
}

const Visuals& FakeModel::visuals() { return visuals_; }

} // namespace cl::ui
