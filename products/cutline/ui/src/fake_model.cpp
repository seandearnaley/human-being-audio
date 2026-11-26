// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/fake_model.h"

#include "cl/engine.h"
#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <cstdint>

namespace cl::ui {

FakeModel::FakeModel() {
    // Showcase a vibrant polymetric Jungle/Amen breakbeat on Cutline 2.0's signature POLY X/Y world!
    params_[ParamId::style] = static_cast<float>(DrumStyle::amen);
    params_[ParamId::gravity] = 0.28f;
    params_[ParamId::ghosts] = 0.66f;
    params_[ParamId::chop] = 0.34f;
    params_[ParamId::ratchet] = 0.42f;
    params_[ParamId::complexity] = 0.68f;
    params_[ParamId::fills] = 0.50f;
    params_[ParamId::dynamics] = 0.72f;

    params_[ParamId::padProgram] = static_cast<float>(PadProgram::poly);
    params_[ParamId::padX] = 0.64f;
    params_[ParamId::padY] = 0.66f;
    params_[ParamId::padTouch] = 1.0f;
    params_[ParamId::padCcMode] = static_cast<float>(PadCcMode::cc74AndCc1);

    auto& sc0 = bank_.scenes[0];
    sc0.customized = true;
    sc0.lanes[0].locked = true;
    sc0.lanes[1].locked = true;
    sc0.lanes[4].stepLength = 16;
    sc0.lanes[6].stepLength = 7;
    sc0.lanes[7].stepLength = 5;
    sc0.lanes[4].steps[15].state = StepState::forceOn;
    sc0.lanes[4].steps[15].ratchet = 3;
    sc0.lanes[1].steps[12].flam = true;
    recompile();
}

void FakeModel::set(ParamId id, float real) {
    params_[id] = clampToRange(spec(id), real);
    recompile();
}

void FakeModel::mutateActiveScene() {
    const int scIdx = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    auto& sc = bank_.scenes[static_cast<std::size_t>(scIdx)];
    if (!sc.customized)
        sc = effectiveSceneData(bank_, scIdx);
    mutateUnlockedLanes(sc, ++actionSeed_);
    recompile();
}

void FakeModel::polymeterActiveScene() {
    const int scIdx = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    auto& sc = bank_.scenes[static_cast<std::size_t>(scIdx)];
    if (!sc.customized)
        sc = effectiveSceneData(bank_, scIdx);
    applyPolymeterToUnlocked(sc, ++actionSeed_);
    recompile();
}

void FakeModel::cloneActiveSceneToNext() {
    const int cur = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    const int next = (cur + 1) % kSceneCount;
    cloneSceneSlot(bank_, cur, next, true, ++actionSeed_);
    set(ParamId::scene, static_cast<float>(next));
}

void FakeModel::clearActiveScene() {
    const int scIdx = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    clearSceneEdits(bank_.scenes[static_cast<std::size_t>(scIdx)]);
    recompile();
}

void FakeModel::recompile() {
    HeldNotes held;
    const int scIdx = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    compilePhrase(params_, scIdx, 0, held, visuals_.plan, bank_, 172.0);
    visuals_.bpm = 172.0;
    visuals_.phraseIndex = 0;
    visuals_.activeScene = scIdx;
    visuals_.pendingScene = -1;
    visuals_.activePadProgram = std::clamp(params_.choice(ParamId::padProgram), 0, kPerfPadCount - 1);
    visuals_.pendingPadProgram = -1;
    visuals_.padEngaged = params_.on(ParamId::padTouch) || params_.on(ParamId::padHold);
    visuals_.midiPadMask = 0;
    visuals_.playheadBeats = 3.0; // Step 12 — snare backbeat hit!
    visuals_.ppq = 3.0;
    visuals_.playing = true;
    visuals_.lastBlockEvents = 18;
    visuals_.activityMask = kActNote | kActCc | (visuals_.padEngaged ? kActPad : 0);
    visuals_.laneFlash = {0.35f, 0.96f, 0.48f, 0.82f, 0.68f, 0.22f, 0.74f, 0.52f};
    for (int l = 0; l < kLaneCount; ++l) {
        const auto idx = static_cast<std::size_t>(l);
        visuals_.lanePhase[idx] = static_cast<float>(
            lanePhase(visuals_.ppq, laneLengthQuarters(params_, l), params_[laneParam(l, LaneField::phase)]));
        visuals_.laneValue[idx] = static_cast<float>(laneSend(params_, curves_, l, visuals_.ppq));
    }
}

const Visuals& FakeModel::visuals() { return visuals_; }

} // namespace cl::ui
