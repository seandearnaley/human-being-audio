// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/fake_model.h"

#include "cl/phrase.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cl::ui {

namespace {

void put(Pattern& p, int lane, int step, int velocity, Layer layer) {
    Step& s = p.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
    s.velocity = static_cast<std::uint8_t>(velocity);
    s.layer = layer;
}

// A two-bar break for screenshots: every layer, ratchets, flams, nudges, a lane pocket, a short lane and a drawn fill.
Pattern showcasePattern() {
    Pattern p{};
    p.bars = 2;
    for (const int step : {0, 10, 16, 26})
        put(p, 0, step, 118, Layer::core);
    put(p, 0, 2, 96, Layer::groove);
    put(p, 0, 19, 96, Layer::detail);
    for (const int step : {4, 12, 20, 28})
        put(p, 1, step, 118, Layer::core);
    put(p, 1, 27, 96, Layer::groove);
    for (const int step : {7, 9, 23, 25})
        put(p, 2, step, 56, Layer::detail);
    put(p, 3, 12, 96, Layer::groove);
    for (int step = 0; step < 32; step += 2)
        put(p, 4, step, step % 4 == 0 ? 118 : 96, Layer::core);
    for (int step = 1; step < 32; step += 4)
        put(p, 4, step, 56, Layer::detail);
    p.lanes[4].steps[14].ratchet = 2;
    put(p, 5, 14, 96, Layer::groove);
    put(p, 5, 30, 96, Layer::groove);
    for (const int step : {0, 3, 6})
        put(p, 6, step, 96, Layer::groove);
    p.lanes[6].length = 7;
    put(p, 7, 0, 96, Layer::core);
    p.lanes[1].steps[12].flam = true;
    p.lanes[1].steps[27].flam = true;
    p.lanes[1].steps[27].nudge = -6; // a pushed, flammed snare: the grace hit lands in the step before
    p.lanes[0].steps[10].nudge = 8;  // a laid-back kick
    p.lanes[4].steps[30].ratchet = 3;
    p.lanes[4].steps[22].nudge = 24; // a full late nudge: on the next step's start
    p.lanes[6].offsetMs = 10;        // perc A sits 10 ms late
    p.fill.beats = 2;
    for (const int step : {8, 10, 12, 14})
        p.fill.steps[1][static_cast<std::size_t>(step)].velocity = 96;
    p.fill.steps[1][15].velocity = 118;
    p.fill.steps[0][8].velocity = 96;
    return p;
}

} // namespace

FakeModel::FakeModel() {
    params_[ParamId::density] = 0.6f;
    params_[ParamId::swing] = 0.6f;
    params_[ParamId::fillEvery] = 2.0f; // every 4 bars
    GrooveBank bank;
    bank.scenes[0] = showcasePattern();
    groove_ = GrooveState(bank);
    pending_ = true;
    refresh();
}

void FakeModel::setGrooveBank(const GrooveBank& bank) {
    if (!inGesture_ || !gestureSnapshot_)
        ++undoSnapshots_;
    gestureSnapshot_ = inGesture_;
    if (inGesture_)
        gestureDirty_ = true;
    else
        ++stateWrites_;
    groove_ = GrooveState(bank);
    refresh();
}

void FakeModel::beginGrooveEdit() {
    inGesture_ = true;
    gestureSnapshot_ = false;
    gestureDirty_ = false;
}

void FakeModel::endGrooveEdit() {
    if (inGesture_ && gestureDirty_)
        ++stateWrites_;
    inGesture_ = false;
    gestureSnapshot_ = false;
    gestureDirty_ = false;
}

void FakeModel::showPending() {
    pending_ = true;
    refresh();
}

void FakeModel::set(ParamId id, float real) {
    params_[id] = clampToRange(spec(id), real);
    refresh();
}

void FakeModel::cloneActiveSceneToNext() {
    const int cur = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    const int next = (cur + 1) % kSceneCount;
    GrooveBank bank = groove_.bank;
    bank.scenes[static_cast<std::size_t>(next)] = bank.scenes[static_cast<std::size_t>(cur)];
    groove_ = GrooveState(bank);
    set(ParamId::scene, static_cast<float>(next));
}

void FakeModel::clearActiveScene() {
    if (params_.choice(ParamId::engineMode) == static_cast<int>(EngineMode::slicer)) {
        // MIDI Slicer: Clear empties the gate strip and leaves the scene alone.
        GrooveBank bank = groove_.bank;
        bank.slicerLength = 16;
        bank.slicerSteps = {};
        groove_ = GrooveState(bank);
        refresh();
        return;
    }
    const int scene = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    GrooveBank bank = groove_.bank;
    bank.scenes[static_cast<std::size_t>(scene)] = Pattern{};
    groove_ = GrooveState(bank);
    refresh();
}

void FakeModel::showBars(int bars) {
    GrooveBank bank = groove_.bank;
    setPatternBars(bank.scenes[0], bars);
    groove_ = GrooveState(bank);
    refresh();
}

void FakeModel::showLive() {
    live_ = true;
    refresh();
}

void FakeModel::showSlicer() { set(ParamId::engineMode, static_cast<float>(EngineMode::slicer)); }

void FakeModel::refresh() {
    const int scene = std::clamp(params_.choice(ParamId::scene), 0, kSceneCount - 1);
    visuals_.bpm = 172.0;
    visuals_.activeScene = scene;
    visuals_.pendingScene = -1;
    visuals_.activePadProgram = std::clamp(params_.choice(ParamId::padProgram), 0, kPerfPadCount - 1);
    visuals_.pendingPadProgram = -1;
    visuals_.padEngaged = params_.on(ParamId::padTouch) || params_.on(ParamId::padHold);
    visuals_.midiPadMask = 0;
    // Host bar 13 (the fifth of the phrase 9-16), bar 1 of the pattern, just after the flammed snare backbeat, so the
    // flam is not under the playhead. A switch to scene B waits for the next bar.
    const int bars = patternBars(groove_.bank.scenes[static_cast<std::size_t>(scene)]);
    visuals_.position = {};
    visuals_.position.absBar = 12;
    visuals_.position.barInPattern = 12 % bars;
    visuals_.position.stepInBar = 13;
    if (pending_)
        visuals_.pendingScene = (scene + 1) % kSceneCount;
    if (live_) {
        // Host bar 4, the last bar of the pattern, inside its fill window, with Repeat holding beat 2 of that bar.
        visuals_.position.absBar = 3;
        visuals_.position.barInPattern = 3 % bars;
        visuals_.position.stepInBar = 13;
        visuals_.position.fillLive = true;
        visuals_.position.repeatStartStep = (bars - 1) * kStepsPerBar + 4;
        visuals_.position.repeatBeats = 1.0;
        visuals_.padEngaged = true;
    }
    visuals_.ppq = 3.0;
    visuals_.playing = true;
    visuals_.lastBlockEvents = 6;
    visuals_.activityMask = static_cast<std::uint8_t>(kActNote | (visuals_.padEngaged ? kActPad : 0));
    visuals_.laneFlash = {0.35f, 0.96f, 0.0f, 0.0f, 0.68f, 0.0f, 0.0f, 0.0f};
    visuals_.heldNotes.clear();
    for (const int note : {48, 55, 60})
        visuals_.heldNotes.push(note, 100);
    compileSlicer(params_, groove_, scene, visuals_.heldNotes, visuals_.bpm, visuals_.slicerPlan);
    for (int l = 0; l < kLaneCount; ++l) {
        const auto idx = static_cast<std::size_t>(l);
        visuals_.lanePhase[idx] = static_cast<float>(
            lanePhase(visuals_.ppq, laneLengthQuarters(params_, l), params_[laneParam(l, LaneField::phase)]));
        visuals_.laneValue[idx] = static_cast<float>(laneSend(params_, curves_, l, visuals_.ppq));
    }
}

const Visuals& FakeModel::visuals() { return visuals_; }

} // namespace cl::ui
