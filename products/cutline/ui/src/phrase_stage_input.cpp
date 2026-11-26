// SPDX-License-Identifier: AGPL-3.0-or-later
#include "phrase_stage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cl::ui {

void PhraseStage::mouseMove(const visage::MouseEvent& e) {
    updateHover(e.position.x, e.position.y);
    redraw();
}

void PhraseStage::mouseExit(const visage::MouseEvent&) {
    if (dragMode_ == DragMode::none) {
        hoveredNote_ = -1;
        hoveredNoteEdge_ = false;
        hoveredSpan_ = -1;
        hoveredChordBar_ = -1;
        hoveredDivider_ = 0;
        hoveredVelLane_ = false;
        hoveredVelNote_ = -1;
        hoveredReset_ = false;
        redraw();
    }
}

void PhraseStage::mouseDown(const visage::MouseEvent& e) {
    const float mx = e.position.x;
    const float my = e.position.y;
    updateHover(mx, my);

    // Reset edits pill
    if (hoveredReset_) {
        context_.model.clearOverrides();
        hoveredReset_ = false;
        redraw();
        return;
    }

    const auto& v = context_.model.visuals();
    const auto& plan = v.plan;
    const auto mode =
        static_cast<OutputMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::outputMode) + 0.5f), 0, 2));
    const Layout lay = computeLayout(plan, mode, width(), height());

    // 1. Tape divider drag
    if (!e.shouldTriggerPopup() && hoveredDivider_ != 0) {
        dragMode_ = hoveredDivider_ == 1 ? DragMode::fillDivider : DragMode::turnDivider;
        return;
    }

    // 2. Tape span click (lock/pin) or right-click span ops menu
    if (hoveredSpan_ >= 0 && hoveredSpan_ < plan.spanCount && hoveredSpan_ < PhraseOverrides::kMaxSpanOverrides) {
        if (e.shouldTriggerPopup()) {
            showSpanMenu(hoveredSpan_);
            return;
        }
        auto ov = context_.model.overrides();
        auto& sp = ov.spans[static_cast<std::size_t>(hoveredSpan_)];
        sp.locked = !sp.locked;
        if (sp.locked)
            sp.lockedSeed = static_cast<std::uint16_t>(
                std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::seed))), 1, 999));
        context_.model.setOverrides(ov);
        redraw();
        return;
    }

    // 2b. Chord progression bar pill (drag vertically for scale degree, double/Alt reset, right-click progressions)
    if (hoveredChordBar_ >= 0 && hoveredChordBar_ < PhraseOverrides::kMaxChordBars) {
        if (e.shouldTriggerPopup()) {
            showChordMenu(hoveredChordBar_);
            return;
        }
        if (e.isAltDown() || e.repeatClickCount() >= 2) {
            auto ov = context_.model.overrides();
            ov.barChordOffsets[static_cast<std::size_t>(hoveredChordBar_)] = 0;
            context_.model.setOverrides(ov);
            redraw();
            return;
        }
        dragMode_ = DragMode::chordBar;
        dragChordBar_ = hoveredChordBar_;
        dragStartY_ = my;
        dragBaseChordDeg_ =
            static_cast<int>(context_.model.overrides().barChordOffsets[static_cast<std::size_t>(dragChordBar_)]);
        return;
    }

    // 3. Direct Velocity Stem Brush in VEL lane
    if (!e.shouldTriggerPopup() && hoveredVelLane_) {
        dragMode_ = DragMode::velBrush;
        velBrushLastX_ = mx;
        captureVelBrushSnapshot(plan, lay);
        applyVelBrush(mx, mx, my, lay);
        return;
    }

    // 4. Piano roll note interaction
    if (hoveredNote_ >= 0 && hoveredNote_ < plan.noteCount) {
        const auto& n = plan.notes[static_cast<std::size_t>(hoveredNote_)];
        if (!isEditableNote(n))
            return;
        if (e.shouldTriggerPopup()) {
            showNoteMenu(n.key, n.slotIndex, n.muted);
            return;
        }
        if (e.isAltDown() || e.repeatClickCount() >= 2) {
            auto ov = context_.model.overrides();
            if (auto* no = ensureNoteOverride(ov, n.key, n.slotIndex)) {
                no->muted = !no->muted;
                pruneNoteOverride(ov, n.key, n.slotIndex, no);
                context_.model.setOverrides(ov);
                redraw();
            }
            return;
        }
        const auto ov = context_.model.overrides();
        const auto* existing = ov.findNoteFor(n.key, n.slotIndex);
        dragNoteIdx_ = hoveredNote_;
        dragNoteKey_ = n.key;
        dragSlotIndex_ = n.slotIndex;
        dragStartX_ = mx;
        dragStartY_ = my;

        if (hoveredNoteEdge_ && !n.muted) {
            dragMode_ = DragMode::noteLength;
            dragBaseLength_ = existing != nullptr ? static_cast<int>(existing->lengthSixteenths) : 0;
            return;
        }
        if (e.isShiftDown() && !n.muted) {
            dragMode_ = DragMode::noteNudge;
            dragBaseNudge_ = existing != nullptr ? static_cast<int>(existing->microNudgeTicks) : 0;
            return;
        }
        if (mode != OutputMode::drum) {
            dragMode_ = DragMode::notePitch;
            dragBasePitchDelta_ = existing != nullptr ? static_cast<int>(existing->pitchDelta) : 0;
            dragOriginalMidi_ = static_cast<int>(n.noteNumber) - dragBasePitchDelta_;
        }
    }
}

void PhraseStage::mouseDrag(const visage::MouseEvent& e) {
    if (dragMode_ == DragMode::none)
        return;
    const auto& v = context_.model.visuals();
    const auto mode =
        static_cast<OutputMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::outputMode) + 0.5f), 0, 2));
    const Layout lay = computeLayout(v.plan, mode, width(), height());

    if (dragMode_ == DragMode::fillDivider || dragMode_ == DragMode::turnDivider) {
        const float frac = std::clamp((e.position.x - lay.padL) / std::max(1.0f, lay.plotW), 0.50f, 0.96f);
        auto ov = context_.model.overrides();
        if (dragMode_ == DragMode::fillDivider) {
            const float maxFill = ov.turnStartFrac >= 0.0f ? (ov.turnStartFrac - 0.04f) : 0.88f;
            ov.fillStartFrac = std::clamp(frac, 0.52f, maxFill);
        } else {
            const float minTurn = ov.fillStartFrac >= 0.0f ? (ov.fillStartFrac + 0.04f) : 0.74f;
            ov.turnStartFrac = std::clamp(frac, minTurn, 0.96f);
        }
        context_.model.setOverrides(ov);
        redraw();
    } else if (dragMode_ == DragMode::chordBar && dragChordBar_ >= 0 &&
               dragChordBar_ < PhraseOverrides::kMaxChordBars) {
        const float pxPerDeg = std::max(context_.px(10.0f), 8.0f);
        const int deltaDeg = static_cast<int>(std::lround((dragStartY_ - e.position.y) / pxPerDeg));
        const int target = std::clamp(dragBaseChordDeg_ + deltaDeg, -7, 7);
        auto ov = context_.model.overrides();
        if (ov.barChordOffsets[static_cast<std::size_t>(dragChordBar_)] != static_cast<std::int8_t>(target)) {
            ov.barChordOffsets[static_cast<std::size_t>(dragChordBar_)] = static_cast<std::int8_t>(target);
            context_.model.setOverrides(ov);
            redraw();
        }
    } else if (dragMode_ == DragMode::velBrush) {
        applyVelBrush(velBrushLastX_, e.position.x, e.position.y, lay);
        velBrushLastX_ = e.position.x;
    } else if (dragMode_ == DragMode::noteLength && (dragNoteKey_.valid() || dragSlotIndex_ >= 0)) {
        const double pxPerSixteenth = std::max(4.0, static_cast<double>(lay.plotW) / (lay.totalBeats * 4.0));
        const int deltaSixteenths =
            static_cast<int>(std::lround(static_cast<double>(e.position.x - dragStartX_) / pxPerSixteenth));
        const int newLen = std::clamp(dragBaseLength_ + deltaSixteenths, -16, 32);
        auto ov = context_.model.overrides();
        if (auto* no = ensureNoteOverride(ov, dragNoteKey_, dragSlotIndex_)) {
            if (no->lengthSixteenths != static_cast<std::int8_t>(newLen)) {
                no->lengthSixteenths = static_cast<std::int8_t>(newLen);
                pruneNoteOverride(ov, dragNoteKey_, dragSlotIndex_, no);
                context_.model.setOverrides(ov);
                redraw();
            }
        }
    } else if (dragMode_ == DragMode::noteNudge && (dragNoteKey_.valid() || dragSlotIndex_ >= 0)) {
        const double pxPerBeat = std::max(8.0, static_cast<double>(lay.plotW) / lay.totalBeats);
        const double deltaBeats = static_cast<double>(e.position.x - dragStartX_) / pxPerBeat;
        const int deltaTicks = static_cast<int>(std::lround(deltaBeats * 96.0));
        const int newNudge = std::clamp(dragBaseNudge_ + deltaTicks, -24, 24);
        auto ov = context_.model.overrides();
        if (auto* no = ensureNoteOverride(ov, dragNoteKey_, dragSlotIndex_)) {
            if (no->microNudgeTicks != static_cast<std::int8_t>(newNudge)) {
                no->microNudgeTicks = static_cast<std::int8_t>(newNudge);
                pruneNoteOverride(ov, dragNoteKey_, dragSlotIndex_, no);
                context_.model.setOverrides(ov);
                redraw();
            }
        }
    } else if (dragMode_ == DragMode::notePitch && (dragNoteKey_.valid() || dragSlotIndex_ >= 0)) {
        const float pxPerSemi = std::max(context_.px(6.0f), lay.rollH / 28.0f);
        const int rawDelta = static_cast<int>(std::lround((dragStartY_ - e.position.y) / pxPerSemi));
        const int targetRawMidi = std::clamp(dragOriginalMidi_ + dragBasePitchDelta_ + rawDelta, 12, 120);
        int finalMidi = targetRawMidi;
        if (!e.isShiftDown() && !e.isMainModifier()) {
            const int root = std::clamp(static_cast<int>(context_.model.get(ParamId::rootNote) + 0.5f), 0, 11);
            const auto scale =
                static_cast<Scale>(std::clamp(static_cast<int>(context_.model.get(ParamId::scale) + 0.5f), 0, 5));
            finalMidi = snapNoteToScale(targetRawMidi, root, scale);
        }
        const int newPitchDelta = std::clamp(finalMidi - dragOriginalMidi_, -24, 24);
        auto ov = context_.model.overrides();
        if (auto* no = ensureNoteOverride(ov, dragNoteKey_, dragSlotIndex_)) {
            no->pitchDelta = static_cast<std::int8_t>(newPitchDelta);
            pruneNoteOverride(ov, dragNoteKey_, dragSlotIndex_, no);
            context_.model.setOverrides(ov);
            redraw();
        }
    }
}

void PhraseStage::mouseUp(const visage::MouseEvent& e) {
    dragMode_ = DragMode::none;
    dragNoteIdx_ = -1;
    dragNoteKey_ = {};
    dragSlotIndex_ = -1;
    dragChordBar_ = -1;
    velBrushCount_ = 0;
    updateHover(e.position.x, e.position.y);
    redraw();
}

PhraseStage::Layout PhraseStage::computeLayout(const PhrasePlan& plan, OutputMode mode, float w, float h) const {
    const auto px = [this](float d) { return context_.px(d); };
    Layout lay{};
    lay.padL = px(64.0f);
    lay.padR = px(12.0f);
    lay.plotW = std::max(1.0f, w - lay.padL - lay.padR);
    lay.totalBeats = std::max(4.0, plan.phraseBeats);
    lay.rulerY = px(4.0f);
    lay.tapeY = px(20.0f);
    lay.tapeH = px(22.0f);
    if (mode != OutputMode::drum) {
        lay.chordY = px(46.0f);
        lay.chordH = px(19.0f);
        lay.rollY = px(69.0f);
    } else {
        lay.chordY = 0.0f;
        lay.chordH = 0.0f;
        lay.rollY = px(46.0f);
    }
    lay.velH = px(36.0f);
    lay.footerY = h - px(20.0f);
    lay.velY = lay.footerY - lay.velH - px(5.0f);
    lay.rollH = std::max(px(80.0f), lay.velY - lay.rollY - px(5.0f));
    lay.fillDivX = -1.0f;
    lay.turnDivX = -1.0f;
    const auto xOf = [&](double beat) { return lay.padL + static_cast<float>(beat / lay.totalBeats) * lay.plotW; };
    for (int i = 0; i < plan.spanCount; ++i) {
        const auto& sp = plan.spans[static_cast<std::size_t>(i)];
        if (sp.role == Role::ratchet && i > 0)
            lay.fillDivX = xOf(sp.startBeats);
        if (sp.role == Role::turnaround && i > 0)
            lay.turnDivX = xOf(sp.startBeats);
    }
    return lay;
}

void PhraseStage::computePitchRange(const PhrasePlan& plan, OutputMode mode, int& minNote, int& maxNote) {
    minNote = 36;
    maxNote = 66;
    if (mode != OutputMode::drum && plan.noteCount > 0) {
        minNote = 127;
        maxNote = 0;
        for (int i = 0; i < plan.noteCount; ++i) {
            const int n = plan.notes[static_cast<std::size_t>(i)].noteNumber;
            minNote = std::min(minNote, n);
            maxNote = std::max(maxNote, n);
        }
        minNote = std::max(12, minNote - 3);
        maxNote = std::min(120, std::max(minNote + 14, maxNote + 3));
    }
}

PhraseStage::NoteBox PhraseStage::noteBoxOf(const CompiledNote& n, const Layout& lay, OutputMode mode, int minNote,
                                            int maxNote) const {
    const auto px = [this](float d) { return context_.px(d); };
    const auto xOf = [&](double beat) { return lay.padL + static_cast<float>(beat / lay.totalBeats) * lay.plotW; };
    const float xWarped = xOf(n.warpedBeats);
    const float xEnd = xOf(std::min(lay.totalBeats, n.warpedBeats + n.durationBeats));
    const float nw = std::max(px(7.0f), xEnd - xWarped);
    const float velNorm = static_cast<float>(n.velocity) / 127.0f;

    float ny = lay.rollY;
    float nh = px(12.0f);
    if (mode == OutputMode::drum) {
        const float rowH = lay.rollH / 6.0f;
        const int row = std::clamp(static_cast<int>(n.drumRow), 0, 5);
        nh = std::max(px(10.0f), rowH * (0.42f + 0.34f * velNorm));
        ny = lay.rollY + static_cast<float>(5 - row) * rowH + (rowH - nh) * 0.5f;
    } else {
        nh = px(8.5f + 6.5f * velNorm);
        const float norm =
            static_cast<float>(n.noteNumber - minNote) / static_cast<float>(std::max(1, maxNote - minNote));
        ny = lay.rollY + lay.rollH - norm * (lay.rollH - px(16.0f)) - px(8.0f) - nh * 0.5f;
    }
    return {xWarped, ny, nw, nh};
}

void PhraseStage::captureVelBrushSnapshot(const PhrasePlan& plan, const Layout& lay) {
    const auto ov = context_.model.overrides();
    const auto xOf = [&](double beat) { return lay.padL + static_cast<float>(beat / lay.totalBeats) * lay.plotW; };
    velBrushCount_ = 0;
    for (int i = 0; i < plan.noteCount && velBrushCount_ < kMaxPhraseNotes; ++i) {
        const auto& n = plan.notes[static_cast<std::size_t>(i)];
        if (!isEditableNote(n) || n.muted)
            continue;
        const auto* existing = ov.findNoteFor(n.key, n.slotIndex);
        const int curDelta = existing != nullptr ? static_cast<int>(existing->velocityDelta) : 0;
        auto& item = velBrushItems_[static_cast<std::size_t>(velBrushCount_++)];
        item.key = n.key;
        item.slotIndex = n.slotIndex;
        item.planIndex = i;
        item.stemX = xOf(n.warpedBeats) + context_.px(2.0f);
        item.baseVel = std::clamp(static_cast<int>(n.velocity) - curDelta, 1, 127);
    }
}

void PhraseStage::applyVelBrush(float xA, float xB, float my, const Layout& lay) {
    const float top = lay.velY + context_.px(4.0f);
    const float bot = lay.velY + lay.velH - context_.px(3.0f);
    const float norm = std::clamp((bot - my) / std::max(1.0f, bot - top), 0.0f, 1.0f);
    const int targetVel = std::clamp(static_cast<int>(std::lround(16.0f + norm * 111.0f)), 16, 127);
    const float slop = context_.px(7.0f);
    const float loX = std::min(xA, xB) - slop;
    const float hiX = std::max(xA, xB) + slop;

    auto ov = context_.model.overrides();
    bool changed = false;
    int lastHitIdx = -1;
    for (int i = 0; i < velBrushCount_; ++i) {
        const auto& item = velBrushItems_[static_cast<std::size_t>(i)];
        if (item.stemX >= loX && item.stemX <= hiX) {
            if (auto* no = ensureNoteOverride(ov, item.key, item.slotIndex)) {
                const auto nextDelta = static_cast<std::int8_t>(std::clamp(targetVel - item.baseVel, -64, 64));
                if (no->velocityDelta != nextDelta) {
                    no->velocityDelta = nextDelta;
                    pruneNoteOverride(ov, item.key, item.slotIndex, no);
                    changed = true;
                }
                lastHitIdx = item.planIndex;
            }
        }
    }
    if (lastHitIdx >= 0)
        dragNoteIdx_ = lastHitIdx;
    if (changed) {
        context_.model.setOverrides(ov);
        redraw();
    }
}

void PhraseStage::updateHover(float mx, float my) {
    hoveredReset_ = resetBtnX_ >= 0.0f && mx >= resetBtnX_ && mx <= resetBtnX_ + resetBtnW_ && my >= resetBtnY_ &&
                    my <= resetBtnY_ + resetBtnH_;
    hoveredSpan_ = -1;
    hoveredChordBar_ = -1;
    hoveredDivider_ = 0;
    hoveredNote_ = -1;
    hoveredNoteEdge_ = false;
    hoveredVelLane_ = false;
    hoveredVelNote_ = -1;

    const auto& v = context_.model.visuals();
    const auto& plan = v.plan;
    const auto mode =
        static_cast<OutputMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::outputMode) + 0.5f), 0, 2));
    const Layout lay = computeLayout(plan, mode, width(), height());
    const auto xOf = [&](double beat) { return lay.padL + static_cast<float>(beat / lay.totalBeats) * lay.plotW; };

    if (my >= lay.tapeY && my <= lay.tapeY + lay.tapeH) {
        const float grab = context_.px(7.0f);
        if (lay.fillDivX > 0.0f && std::abs(mx - lay.fillDivX) <= grab) {
            hoveredDivider_ = 1;
            return;
        }
        if (lay.turnDivX > 0.0f && std::abs(mx - lay.turnDivX) <= grab) {
            hoveredDivider_ = 2;
            return;
        }
        for (int i = 0; i < plan.spanCount; ++i) {
            const auto& sp = plan.spans[static_cast<std::size_t>(i)];
            if (mx >= xOf(sp.startBeats) && mx <= xOf(sp.endBeats)) {
                hoveredSpan_ = i;
                return;
            }
        }
        return;
    }

    if (lay.chordH > 0.0f && my >= lay.chordY && my <= lay.chordY + lay.chordH && mx >= lay.padL &&
        mx <= lay.padL + lay.plotW) {
        const int barCount =
            std::clamp(static_cast<int>(std::lround(lay.totalBeats)) / 4, 1, PhraseOverrides::kMaxChordBars);
        const int bar =
            std::clamp(static_cast<int>(((mx - lay.padL) / std::max(1.0f, lay.plotW)) * static_cast<float>(barCount)),
                       0, barCount - 1);
        hoveredChordBar_ = bar;
        return;
    }

    if (my >= lay.rollY && my <= lay.rollY + lay.rollH) {
        int minNote = 36;
        int maxNote = 66;
        computePitchRange(plan, mode, minNote, maxNote);
        const float slop = context_.px(3.0f);
        const float edgeGrab = context_.px(6.0f);
        for (int i = plan.noteCount - 1; i >= 0; --i) {
            const auto& n = plan.notes[static_cast<std::size_t>(i)];
            if (!isEditableNote(n))
                continue;
            const NoteBox box = noteBoxOf(n, lay, mode, minNote, maxNote);
            if (mx >= box.x - slop && mx <= box.x + box.w + slop && my >= box.y - slop && my <= box.y + box.h + slop) {
                hoveredNote_ = i;
                hoveredNoteEdge_ = !n.muted && box.w >= context_.px(10.0f) && mx >= box.x + box.w - edgeGrab;
                return;
            }
        }
        return;
    }

    if (my >= lay.velY && my <= lay.velY + lay.velH && mx >= lay.padL && mx <= lay.padL + lay.plotW) {
        hoveredVelLane_ = true;
        float bestDist = context_.px(10.0f);
        for (int i = 0; i < plan.noteCount; ++i) {
            const auto& n = plan.notes[static_cast<std::size_t>(i)];
            if (!isEditableNote(n))
                continue;
            const float stemX = xOf(n.warpedBeats) + context_.px(2.0f);
            const float d = std::abs(mx - stemX);
            if (d < bestDist) {
                bestDist = d;
                hoveredVelNote_ = i;
            }
        }
    }
}

void PhraseStage::showChordMenu(int barIdx) {
    const auto ov = context_.model.overrides();
    const int cur = static_cast<int>(ov.barChordOffsets[static_cast<std::size_t>(barIdx)]);
    const auto& plan = context_.model.visuals().plan;
    const int barCount = std::clamp(static_cast<int>(std::lround(std::max(4.0, plan.phraseBeats))) / 4, 1,
                                    PhraseOverrides::kMaxChordBars);

    visage::PopupMenu menu;
    menu.addOption(1, "Bar: Tonic i (0)").select(cur == 0);
    menu.addOption(2, "Bar: +1 scale degree");
    menu.addOption(3, "Bar: -1 scale degree");
    menu.addOption(4, "Bar: bVI (-2 deg)").select(cur == -2);
    menu.addOption(5, "Bar: bVII (-1 deg)").select(cur == -1);
    menu.addOption(6, "Bar: iv (+3 deg)").select(cur == 3);
    menu.addOption(7, "Bar: v (+4 deg)").select(cur == 4);
    menu.addOption(10, "Progression: Pedal (i \u2013 i \u2013 i \u2013 i)");
    menu.addOption(11, "Progression: Dark Neuro (i \u2013 i \u2013 bII \u2013 i)");
    menu.addOption(12, "Progression: Epic Minor (i \u2013 bVI \u2013 bVII \u2013 i)");
    menu.addOption(13, "Progression: Liquid Roll (i \u2013 bIII \u2013 bVI \u2013 bVII)");
    menu.addOption(14, "Progression: Dorian Lift (i \u2013 iv \u2013 bVII \u2013 v)");
    menu.addOption(15, "Progression: Tension Climb (i \u2013 bII \u2013 bIII \u2013 bII)");
    menu.addOption(20, "Reset All Chords").enable(ov.hasChordProgression());

    menu.onSelection() = [this, barIdx, barCount](int opt) {
        auto next = context_.model.overrides();
        const auto applyPattern = [&](std::array<std::int8_t, 4> pat) {
            next.barChordOffsets = {};
            for (int b = 0; b < barCount; ++b)
                next.barChordOffsets[static_cast<std::size_t>(b)] = pat[static_cast<std::size_t>(b % 4)];
        };
        const auto idx = static_cast<std::size_t>(barIdx);
        if (opt == 1)
            next.barChordOffsets[idx] = 0;
        else if (opt == 2)
            next.barChordOffsets[idx] =
                static_cast<std::int8_t>(std::clamp(static_cast<int>(next.barChordOffsets[idx]) + 1, -7, 7));
        else if (opt == 3)
            next.barChordOffsets[idx] =
                static_cast<std::int8_t>(std::clamp(static_cast<int>(next.barChordOffsets[idx]) - 1, -7, 7));
        else if (opt == 4)
            next.barChordOffsets[idx] = -2;
        else if (opt == 5)
            next.barChordOffsets[idx] = -1;
        else if (opt == 6)
            next.barChordOffsets[idx] = 3;
        else if (opt == 7)
            next.barChordOffsets[idx] = 4;
        else if (opt == 10 || opt == 20)
            next.barChordOffsets = {};
        else if (opt == 11)
            applyPattern({0, 0, 1, 0});
        else if (opt == 12)
            applyPattern({0, -2, -1, 0});
        else if (opt == 13)
            applyPattern({0, 2, -2, -1});
        else if (opt == 14)
            applyPattern({0, 3, -1, 4});
        else if (opt == 15)
            applyPattern({0, 1, 2, 1});
        context_.model.setOverrides(next);
        redraw();
    };
    menu.show(this);
}

void PhraseStage::showSpanMenu(int spanIdx) {
    const auto ov = context_.model.overrides();
    const auto& sp = ov.spans[static_cast<std::size_t>(spanIdx)];
    const auto& plan = context_.model.visuals().plan;
    const bool canCopyNext = (spanIdx + 1 < plan.spanCount && spanIdx + 1 < PhraseOverrides::kMaxSpanOverrides);

    visage::PopupMenu menu;
    menu.addOption(100, sp.locked ? "Unlock Span" : "Lock Span (Pin Seed)");
    menu.addOption(101, "Roll This Span (New Seed)");
    menu.addOption(102, "Copy Span \u2192 Next").enable(canCopyNext);
    menu.addOption(110, "Octave: +1").enable(sp.octaveShift < 2);
    menu.addOption(111, "Octave: -1").enable(sp.octaveShift > -2);
    menu.addOption(112, "Octave: Reset (0)").enable(sp.octaveShift != 0);
    menu.addOption(115, "Invert Contour").select(sp.invert);
    menu.addOption(120, "Time: Half (\u00bd\u00d7)").select(sp.timeScale < 0);
    menu.addOption(121, "Time: Normal (1\u00d7)").select(sp.timeScale == 0);
    menu.addOption(122, "Time: Double (2\u00d7)").select(sp.timeScale > 0);
    menu.addOption(0, "Role: Auto").select(sp.roleOverride < 0);
    menu.addOption(1, "Role: Statement").select(sp.roleOverride == static_cast<int>(Role::statement));
    menu.addOption(2, "Role: Answer").select(sp.roleOverride == static_cast<int>(Role::answer));
    menu.addOption(3, "Role: Mutation").select(sp.roleOverride == static_cast<int>(Role::mutation));
    menu.addOption(4, "Role: Ratchet").select(sp.roleOverride == static_cast<int>(Role::ratchet));
    menu.addOption(5, "Role: Turnaround").select(sp.roleOverride == static_cast<int>(Role::turnaround));
    menu.addOption(6, "Role: Rest (Drop-out)").select(sp.roleOverride == static_cast<int>(Role::rest));
    menu.addOption(199, "Reset This Span").enable(sp.active());
    menu.addOption(200, "Reset All Phrase Edits").enable(ov.hasAny());

    menu.onSelection() = [this, spanIdx](int opt) {
        if (opt == 200) {
            context_.model.clearOverrides();
            redraw();
            return;
        }
        auto next = context_.model.overrides();
        auto& target = next.spans[static_cast<std::size_t>(spanIdx)];
        const auto globalSeed = static_cast<std::uint16_t>(
            std::clamp(static_cast<int>(std::lround(context_.model.get(ParamId::seed))), 1, 999));

        if (opt == 100) {
            target.locked = !target.locked;
            if (target.locked)
                target.lockedSeed = globalSeed;
        } else if (opt == 101) {
            const std::uint32_t base = target.locked && target.lockedSeed > 0 ? target.lockedSeed : globalSeed;
            const auto rolled = static_cast<std::uint16_t>(
                1U + (hba::dsp::laneHash(base * 2654435761U + static_cast<std::uint32_t>(spanIdx + 1) * 97U) % 999U));
            target.locked = true;
            target.lockedSeed = rolled == base ? static_cast<std::uint16_t>((rolled % 999U) + 1U) : rolled;
        } else if (opt == 102 && spanIdx + 1 < PhraseOverrides::kMaxSpanOverrides) {
            const auto& curPlan = context_.model.visuals().plan;
            if (!target.locked) {
                target.locked = true;
                target.lockedSeed = globalSeed;
            }
            next.spans[static_cast<std::size_t>(spanIdx + 1)] = target;
            if (spanIdx + 1 < curPlan.spanCount) {
                const double beatDelta = curPlan.spans[static_cast<std::size_t>(spanIdx + 1)].startBeats -
                                         curPlan.spans[static_cast<std::size_t>(spanIdx)].startBeats;
                const int tickDelta = static_cast<int>(std::lround(beatDelta * 24.0));
                const auto snapshotNotes = next.notes;
                for (const auto& n : snapshotNotes) {
                    if (!n.active() || !n.key.valid() || n.key.spanIdx != spanIdx)
                        continue;
                    NoteKey dstKey = n.key;
                    dstKey.spanIdx = static_cast<std::int8_t>(spanIdx + 1);
                    dstKey.stepTick =
                        static_cast<std::int16_t>(std::max(0, static_cast<int>(n.key.stepTick) + tickDelta));
                    if (auto* dst = next.findOrCreateNote(dstKey)) {
                        *dst = n;
                        dst->key = dstKey;
                        dst->slotIndex = -1;
                    }
                }
            }
        } else if (opt == 110) {
            target.octaveShift = static_cast<std::int8_t>(std::clamp(static_cast<int>(target.octaveShift) + 1, -2, 2));
        } else if (opt == 111) {
            target.octaveShift = static_cast<std::int8_t>(std::clamp(static_cast<int>(target.octaveShift) - 1, -2, 2));
        } else if (opt == 112) {
            target.octaveShift = 0;
        } else if (opt == 115) {
            target.invert = !target.invert;
        } else if (opt == 120) {
            target.timeScale = -1;
        } else if (opt == 121) {
            target.timeScale = 0;
        } else if (opt == 122) {
            target.timeScale = 1;
        } else if (opt == 0) {
            target.roleOverride = -1;
        } else if (opt >= 1 && opt <= 6) {
            target.roleOverride = static_cast<std::int8_t>(opt - 1);
        } else if (opt == 199) {
            target = {};
        }
        context_.model.setOverrides(next);
        redraw();
    };
    menu.show(this);
}

void PhraseStage::showNoteMenu(NoteKey key, int slotIndex, bool currentlyMuted) {
    const auto ov = context_.model.overrides();
    const auto* existing = ov.findNoteFor(key, slotIndex);
    const bool hasNote = existing != nullptr;
    const std::uint8_t curRatchet = existing != nullptr ? existing->ratchetCount : 0;

    visage::PopupMenu menu;
    menu.addOption(1, currentlyMuted ? "Unmute Note" : "Mute Note");
    menu.addOption(2, "Accent (+28 vel)");
    menu.addOption(3, "Ghost (-28 vel)");
    menu.addOption(4, "Flip / Add Pitch Dive");
    menu.addOption(10, "Ratchet: Off").select(curRatchet == 0);
    menu.addOption(12, "Ratchet: \u00d72 Burst").select(curRatchet == 2);
    menu.addOption(13, "Ratchet: \u00d73 Triplet").select(curRatchet == 3);
    menu.addOption(14, "Ratchet: \u00d74 Stutter").select(curRatchet == 4);
    menu.addOption(16, "Ratchet: \u00d76 Roll").select(curRatchet == 6);
    menu.addOption(5, "Reset This Note").enable(hasNote);
    menu.addOption(6, "Reset All Phrase Edits").enable(ov.hasAny());

    menu.onSelection() = [this, key, slotIndex](int opt) {
        if (opt == 6) {
            context_.model.clearOverrides();
            redraw();
            return;
        }
        auto next = context_.model.overrides();
        if (opt == 5) {
            if (key.valid())
                next.clearNote(key);
            if (slotIndex >= 0)
                next.clearNote(slotIndex);
        } else if (auto* no = ensureNoteOverride(next, key, slotIndex)) {
            if (opt == 1)
                no->muted = !no->muted;
            else if (opt == 2)
                no->velocityDelta =
                    static_cast<std::int8_t>(std::clamp(static_cast<int>(no->velocityDelta) + 28, -64, 64));
            else if (opt == 3)
                no->velocityDelta =
                    static_cast<std::int8_t>(std::clamp(static_cast<int>(no->velocityDelta) - 28, -64, 64));
            else if (opt == 4)
                no->flipBend = !no->flipBend;
            else if (opt == 10)
                no->ratchetCount = 0;
            else if (opt == 12)
                no->ratchetCount = 2;
            else if (opt == 13)
                no->ratchetCount = 3;
            else if (opt == 14)
                no->ratchetCount = 4;
            else if (opt == 16)
                no->ratchetCount = 6;
            pruneNoteOverride(next, key, slotIndex, no);
        }
        context_.model.setOverrides(next);
        redraw();
    };
    menu.show(this);
}

} // namespace cl::ui
