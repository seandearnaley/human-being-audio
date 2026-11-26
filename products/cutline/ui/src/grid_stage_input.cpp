// SPDX-License-Identifier: AGPL-3.0-or-later
#include "grid_stage.h"

#include <visage/ui.h>

#include <algorithm>
#include <cmath>

namespace cl::ui {

void GridStagePanel::mouseMove(const visage::MouseEvent& e) {
    const float mx = e.position.x;
    const float my = e.position.y;
    const Layout lay = computeLayout(width(), height());
    const auto& vis = context_.model.visuals();
    const auto mode =
        static_cast<EngineMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::engineMode) + 0.5f), 0, 1));

    hoverLane_ = -1;
    hoverStep_ = -1;
    hoverLengthLane_ = -1;
    hoverSlicerLen_ = false;
    hoverScene_ = -1;
    hoverAction_ = -1;

    for (int i = 0; i < kSceneCount; ++i) {
        if (lay.scenes[static_cast<std::size_t>(i)].contains(mx, my))
            hoverScene_ = i;
    }
    if (lay.btnMutate.contains(mx, my))
        hoverAction_ = 0;
    else if (lay.btnPoly.contains(mx, my))
        hoverAction_ = 1;
    else if (lay.btnClone.contains(mx, my))
        hoverAction_ = 2;
    else if (lay.btnClear.contains(mx, my))
        hoverAction_ = 3;

    if (mode == EngineMode::drum) {
        if (my >= lay.gridY && my <= lay.gridY + lay.gridH) {
            const int l = std::clamp(static_cast<int>((my - lay.gridY) / lay.rowH), 0, kDrumLaneCount - 1);
            const int laneLen = std::clamp(static_cast<int>(vis.plan.laneLengths[static_cast<std::size_t>(l)]), 1,
                                           kMaxLaneSteps);
            const float hx = laneEndHandleX(laneLen, lay);
            if (std::abs(mx - hx) <= px(7.0f)) {
                hoverLengthLane_ = l;
            } else if (mx >= lay.gridX && mx <= lay.gridX + lay.gridW) {
                hoverLane_ = l;
                hoverStep_ = std::clamp(static_cast<int>((mx - lay.gridX) / lay.stepW), 0, kMaxLaneSteps - 1);
            }
        }
    } else {
        const auto& gr = lay.slicerGateRect;
        if (my >= gr.y && my <= gr.y + gr.h) {
            const int slLen = std::clamp(static_cast<int>(vis.plan.slicerLength), 1, kMaxLaneSteps);
            const float hx = laneEndHandleX(slLen, lay);
            if (std::abs(mx - hx) <= px(8.0f)) {
                hoverSlicerLen_ = true;
            } else if (mx >= gr.x && mx <= gr.x + gr.w) {
                hoverLane_ = 99;
                hoverStep_ = std::clamp(static_cast<int>((mx - gr.x) / lay.stepW), 0, kMaxLaneSteps - 1);
            }
        }
    }
    redraw();
}

void GridStagePanel::mouseExit(const visage::MouseEvent&) {
    hoverLane_ = hoverStep_ = hoverLengthLane_ = hoverScene_ = hoverAction_ = -1;
    hoverSlicerLen_ = false;
    redraw();
}

void GridStagePanel::mouseDown(const visage::MouseEvent& e) {
    const float mx = e.position.x;
    const float my = e.position.y;
    const Layout lay = computeLayout(width(), height());
    const auto& vis = context_.model.visuals();
    const auto mode =
        static_cast<EngineMode>(std::clamp(static_cast<int>(context_.model.get(ParamId::engineMode) + 0.5f), 0, 1));
    const int scIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);

    // Header buttons
    if (lay.modeDrum.contains(mx, my)) {
        context_.model.begin(ParamId::engineMode);
        context_.model.set(ParamId::engineMode, static_cast<float>(EngineMode::drum));
        context_.model.end(ParamId::engineMode);
        return;
    }
    if (lay.modeSlicer.contains(mx, my)) {
        context_.model.begin(ParamId::engineMode);
        context_.model.set(ParamId::engineMode, static_cast<float>(EngineMode::slicer));
        context_.model.end(ParamId::engineMode);
        return;
    }
    for (int i = 0; i < kSceneCount; ++i) {
        if (lay.scenes[static_cast<std::size_t>(i)].contains(mx, my)) {
            context_.model.begin(ParamId::scene);
            context_.model.set(ParamId::scene, static_cast<float>(i));
            context_.model.end(ParamId::scene);
            return;
        }
    }
    if (lay.btnMutate.contains(mx, my)) {
        context_.model.mutateActiveScene();
        return;
    }
    if (lay.btnPoly.contains(mx, my)) {
        context_.model.polymeterActiveScene();
        return;
    }
    if (lay.btnClone.contains(mx, my)) {
        context_.model.cloneActiveSceneToNext();
        return;
    }
    if (lay.btnClear.contains(mx, my)) {
        context_.model.clearActiveScene();
        return;
    }

    auto bank = context_.model.patternBank();
    auto ensureSceneCustom = [&]() -> SceneData& {
        auto& sc = bank.scenes[static_cast<std::size_t>(scIdx)];
        if (!sc.customized) {
            sc = effectiveSceneData(bank, scIdx);
            sc.customized = true;
        }
        return sc;
    };

    if (mode == EngineMode::drum) {
        if (my < lay.gridY || my > lay.gridY + lay.gridH)
            return;
        const int lane = std::clamp(static_cast<int>((my - lay.gridY) / lay.rowH), 0, kDrumLaneCount - 1);
        const auto li = static_cast<std::size_t>(lane);

        // Sidebar controls: MIDI Note, Mute, Solo, Lock, or Lane Right-Click
        if (laneMuteRect(lane, lay).contains(mx, my)) {
            auto& sc = ensureSceneCustom();
            sc.lanes[li].muted = !sc.lanes[li].muted;
            context_.model.setPatternBank(bank);
            return;
        }
        if (laneSoloRect(lane, lay).contains(mx, my)) {
            auto& sc = ensureSceneCustom();
            sc.lanes[li].solo = !sc.lanes[li].solo;
            context_.model.setPatternBank(bank);
            return;
        }
        if (laneLockRect(lane, lay).contains(mx, my)) {
            auto& sc = ensureSceneCustom();
            sc.lanes[li].locked = !sc.lanes[li].locked;
            if (sc.lanes[li].locked)
                sc.lanes[li].lockedVar =
                    static_cast<std::uint8_t>(std::clamp(static_cast<int>(context_.model.get(ParamId::variation) + 0.5f), 1, 64));
            context_.model.setPatternBank(bank);
            return;
        }
        if (laneNoteRect(lane, lay).contains(mx, my)) {
            if (e.shouldTriggerPopup()) {
                auto& sc = ensureSceneCustom();
                sc.lanes[li].midiNote = 0; // Revert to active Kit Map default
                context_.model.setPatternBank(bank);
                return;
            }
            dragMode_ = DragMode::laneNote;
            dragLane_ = lane;
            dragStartY_ = my;
            dragBaseNote_ = vis.plan.laneMidiNotes[li];
            return;
        }
        if (mx < lay.gridX) {
            if (e.shouldTriggerPopup())
                showLaneMenu(lane);
            return;
        }

        // Polymeter length drag handle check
        const int laneLen = std::clamp(static_cast<int>(vis.plan.laneLengths[li]), 1, kMaxLaneSteps);
        if (std::abs(mx - laneEndHandleX(laneLen, lay)) <= px(7.0f)) {
            dragMode_ = DragMode::laneLength;
            dragLane_ = lane;
            return;
        }

        // Step grid interaction
        const int step = std::clamp(static_cast<int>((mx - lay.gridX) / lay.stepW), 0, kMaxLaneSteps - 1);
        if (step >= laneLen)
            return;

        if (e.shouldTriggerPopup()) {
            showDrumStepMenu(lane, step);
            return;
        }

        // Shift-click cycles ratchet (off -> x2 -> x3 -> x4 -> off) instantly!
        if (e.isShiftDown()) {
            auto& sc = ensureSceneCustom();
            auto& st = sc.lanes[li].steps[static_cast<std::size_t>(step)];
            st.state = StepState::forceOn;
            st.ratchet = st.ratchet == 0 ? 2 : (st.ratchet == 2 ? 3 : (st.ratchet == 3 ? 4 : 0));
            context_.model.setPatternBank(bank);
            return;
        }

        const bool currentlyOn = vis.plan.gridVel[li][static_cast<std::size_t>(step)] > 0;
        auto& sc = ensureSceneCustom();
        auto& st = sc.lanes[li].steps[static_cast<std::size_t>(step)];

        if (currentlyOn) {
            // Allow vertical drag to sculpt velocity, or quick click to toggle OFF
            dragMode_ = DragMode::velocity;
            dragLane_ = lane;
            dragStep_ = step;
            dragStartY_ = my;
            dragBaseVelOff_ = static_cast<int>(st.velOffset);
        } else {
            st.state = StepState::forceOn;
            dragMode_ = DragMode::paintOn;
            dragLane_ = lane;
            dragStep_ = step;
            context_.model.setPatternBank(bank);
        }
    } else {
        // Slicer Gate Strip interactions
        const auto& gr = lay.slicerGateRect;
        if (my < gr.y || my > gr.y + gr.h)
            return;
        const int slLen = std::clamp(static_cast<int>(vis.plan.slicerLength), 1, kMaxLaneSteps);
        if (std::abs(mx - laneEndHandleX(slLen, lay)) <= px(8.0f)) {
            dragMode_ = DragMode::slicerLength;
            return;
        }
        if (mx < gr.x || mx > gr.x + gr.w)
            return;
        const int step = std::clamp(static_cast<int>((mx - gr.x) / lay.stepW), 0, kMaxLaneSteps - 1);
        if (step >= slLen)
            return;

        if (e.shouldTriggerPopup()) {
            showSlicerStepMenu(step);
            return;
        }
        if (e.isShiftDown()) {
            auto& st = bank.slicerSteps[static_cast<std::size_t>(step)];
            st.state = StepState::forceOn;
            st.ratchet = st.ratchet == 0 ? 2 : (st.ratchet == 2 ? 3 : (st.ratchet == 3 ? 4 : 0));
            context_.model.setPatternBank(bank);
            return;
        }

        const bool on = vis.plan.slicerVel[static_cast<std::size_t>(step)] > 0;
        auto& st = bank.slicerSteps[static_cast<std::size_t>(step)];
        if (on) {
            dragMode_ = DragMode::velocity;
            dragLane_ = 99;
            dragStep_ = step;
            dragStartY_ = my;
            dragBaseVelOff_ = static_cast<int>(st.velOffset);
        } else {
            st.state = StepState::forceOn;
            dragMode_ = DragMode::paintOn;
            dragLane_ = 99;
            dragStep_ = step;
            context_.model.setPatternBank(bank);
        }
    }
}

void GridStagePanel::mouseDrag(const visage::MouseEvent& e) {
    if (dragMode_ == DragMode::none)
        return;

    const float mx = e.position.x;
    const float my = e.position.y;
    const Layout lay = computeLayout(width(), height());
    const int scIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
    auto bank = context_.model.patternBank();
    auto ensureSceneCustom = [&]() -> SceneData& {
        auto& sc = bank.scenes[static_cast<std::size_t>(scIdx)];
        if (!sc.customized) {
            sc = effectiveSceneData(bank, scIdx);
            sc.customized = true;
        }
        return sc;
    };

    if (dragMode_ == DragMode::laneLength && dragLane_ >= 0 && dragLane_ < kDrumLaneCount) {
        const int newLen = std::clamp(static_cast<int>(std::lround((mx - lay.gridX) / lay.stepW)), 1, kMaxLaneSteps);
        auto& sc = ensureSceneCustom();
        if (sc.lanes[static_cast<std::size_t>(dragLane_)].stepLength != static_cast<std::uint8_t>(newLen)) {
            sc.lanes[static_cast<std::size_t>(dragLane_)].stepLength = static_cast<std::uint8_t>(newLen);
            context_.model.setPatternBank(bank);
        }
        return;
    }

    if (dragMode_ == DragMode::slicerLength) {
        const int newLen = std::clamp(static_cast<int>(std::lround((mx - lay.gridX) / lay.stepW)), 1, kMaxLaneSteps);
        if (bank.slicerLength != static_cast<std::uint8_t>(newLen)) {
            bank.slicerLength = static_cast<std::uint8_t>(newLen);
            context_.model.setPatternBank(bank);
        }
        return;
    }

    if (dragMode_ == DragMode::laneNote && dragLane_ >= 0 && dragLane_ < kDrumLaneCount) {
        const int delta = static_cast<int>(std::lround((dragStartY_ - my) / px(5.0f)));
        const int nextNote = std::clamp(dragBaseNote_ + delta, 24, 96);
        auto& sc = ensureSceneCustom();
        if (sc.lanes[static_cast<std::size_t>(dragLane_)].midiNote != static_cast<std::uint8_t>(nextNote)) {
            sc.lanes[static_cast<std::size_t>(dragLane_)].midiNote = static_cast<std::uint8_t>(nextNote);
            context_.model.setPatternBank(bank);
        }
        return;
    }

    if (dragMode_ == DragMode::velocity) {
        const float dy = dragStartY_ - my;
        if (std::abs(dy) < px(4.0f))
            return;
        const int nextVel = std::clamp(dragBaseVelOff_ + static_cast<int>(std::lround(dy * 1.4f)), -64, 64);
        if (dragLane_ == 99) {
            bank.slicerSteps[static_cast<std::size_t>(dragStep_)].velOffset = static_cast<std::int8_t>(nextVel);
            context_.model.setPatternBank(bank);
        } else if (dragLane_ >= 0 && dragLane_ < kDrumLaneCount) {
            auto& sc = ensureSceneCustom();
            sc.lanes[static_cast<std::size_t>(dragLane_)].steps[static_cast<std::size_t>(dragStep_)].velOffset =
                static_cast<std::int8_t>(nextVel);
            context_.model.setPatternBank(bank);
        }
        return;
    }

    if (dragMode_ == DragMode::paintOn) {
        const int step = std::clamp(static_cast<int>((mx - lay.gridX) / lay.stepW), 0, kMaxLaneSteps - 1);
        if (step != dragStep_) {
            dragStep_ = step;
            if (dragLane_ == 99) {
                bank.slicerSteps[static_cast<std::size_t>(step)].state = StepState::forceOn;
                context_.model.setPatternBank(bank);
            } else if (dragLane_ >= 0 && dragLane_ < kDrumLaneCount) {
                auto& sc = ensureSceneCustom();
                sc.lanes[static_cast<std::size_t>(dragLane_)].steps[static_cast<std::size_t>(step)].state =
                    StepState::forceOn;
                context_.model.setPatternBank(bank);
            }
        }
    }
}

void GridStagePanel::mouseUp(const visage::MouseEvent& e) {
    // Quick click without vertical drag on an already-active step toggles it OFF cleanly!
    if (dragMode_ == DragMode::velocity && std::abs(e.position.y - dragStartY_) < px(4.0f)) {
        const int scIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
        auto bank = context_.model.patternBank();
        if (dragLane_ == 99) {
            bank.slicerSteps[static_cast<std::size_t>(dragStep_)].state = StepState::forceOff;
            context_.model.setPatternBank(bank);
        } else if (dragLane_ >= 0 && dragLane_ < kDrumLaneCount) {
            auto& sc = bank.scenes[static_cast<std::size_t>(scIdx)];
            if (!sc.customized) {
                sc = effectiveSceneData(bank, scIdx);
                sc.customized = true;
            }
            sc.lanes[static_cast<std::size_t>(dragLane_)].steps[static_cast<std::size_t>(dragStep_)].state =
                StepState::forceOff;
            context_.model.setPatternBank(bank);
        }
    }
    dragMode_ = DragMode::none;
    dragLane_ = -1;
    dragStep_ = -1;
}

void GridStagePanel::showDrumStepMenu(int lane, int step) {
    visage::PopupMenu menu;
    menu.addOption(1, "Ratchet: Off");
    menu.addOption(2, "Ratchet: \u00d72 (32nd burst)");
    menu.addOption(3, "Ratchet: \u00d73 (Triplet burst)");
    menu.addOption(4, "Ratchet: \u00d74 (64th roll)");
    menu.addBreak();
    menu.addOption(10, "Toggle Grace Flam");
    menu.addOption(11, "Nudge Early (-1/32)");
    menu.addOption(12, "Nudge Late (+1/32)");
    menu.addOption(13, "Reset Step to Auto");
    menu.onSelection() += [this, lane, step](int choice) {
        const int scIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
        auto bank = context_.model.patternBank();
        auto& sc = bank.scenes[static_cast<std::size_t>(scIdx)];
        if (!sc.customized) {
            sc = effectiveSceneData(bank, scIdx);
            sc.customized = true;
        }
        auto& st = sc.lanes[static_cast<std::size_t>(lane)].steps[static_cast<std::size_t>(step)];
        if (choice == 1) {
            st.ratchet = 0;
        } else if (choice >= 2 && choice <= 4) {
            st.state = StepState::forceOn;
            st.ratchet = static_cast<std::uint8_t>(choice);
        } else if (choice == 10) {
            st.state = StepState::forceOn;
            st.flam = !st.flam;
        } else if (choice == 11) {
            st.microNudge = static_cast<std::int8_t>(std::clamp(static_cast<int>(st.microNudge) - 6, -24, 24));
        } else if (choice == 12) {
            st.microNudge = static_cast<std::int8_t>(std::clamp(static_cast<int>(st.microNudge) + 6, -24, 24));
        } else if (choice == 13) {
            st = {};
        }
        context_.model.setPatternBank(bank);
    };
    menu.show(this);
}

void GridStagePanel::showSlicerStepMenu(int step) {
    visage::PopupMenu menu;
    menu.addOption(1, "Ratchet: Off");
    menu.addOption(2, "Ratchet: \u00d72");
    menu.addOption(3, "Ratchet: \u00d73");
    menu.addOption(4, "Ratchet: \u00d74");
    menu.addBreak();
    menu.addOption(10, "Toggle Chord Strum");
    menu.addOption(13, "Reset Step to Auto");
    menu.onSelection() += [this, step](int choice) {
        auto bank = context_.model.patternBank();
        auto& st = bank.slicerSteps[static_cast<std::size_t>(step)];
        if (choice == 1)
            st.ratchet = 0;
        else if (choice >= 2 && choice <= 4) {
            st.state = StepState::forceOn;
            st.ratchet = static_cast<std::uint8_t>(choice);
        } else if (choice == 10) {
            st.state = StepState::forceOn;
            st.flam = !st.flam;
        } else if (choice == 13) {
            st = {};
        }
        context_.model.setPatternBank(bank);
    };
    menu.show(this);
}

void GridStagePanel::showLaneMenu(int lane) {
    visage::PopupMenu menu;
    menu.addOption(1, "Length: 16 steps");
    menu.addOption(2, "Length: 32 steps (2 bars)");
    menu.addOption(3, "Polymeter: 7 steps");
    menu.addOption(4, "Polymeter: 11 steps");
    menu.addOption(5, "Polymeter: 12 steps");
    menu.addBreak();
    menu.addOption(10, "Euclidean: 3 hits");
    menu.addOption(11, "Euclidean: 5 hits");
    menu.addOption(12, "Euclidean: 7 hits");
    menu.addOption(13, "Euclidean: Auto (Style)");
    menu.addBreak();
    menu.addOption(20, "Reset Lane");
    menu.addOption(21, "Reset All Custom Notes to Kit Map");
    menu.onSelection() += [this, lane](int choice) {
        const int scIdx = std::clamp(static_cast<int>(context_.model.get(ParamId::scene) + 0.5f), 0, kSceneCount - 1);
        auto bank = context_.model.patternBank();
        auto& sc = bank.scenes[static_cast<std::size_t>(scIdx)];
        if (!sc.customized) {
            sc = effectiveSceneData(bank, scIdx);
            sc.customized = true;
        }
        auto& l = sc.lanes[static_cast<std::size_t>(lane)];
        if (choice == 1)
            l.stepLength = 16;
        else if (choice == 2)
            l.stepLength = 32;
        else if (choice == 3)
            l.stepLength = 7;
        else if (choice == 4)
            l.stepLength = 11;
        else if (choice == 5)
            l.stepLength = 12;
        else if (choice == 10)
            l.euclidHits = 3;
        else if (choice == 11)
            l.euclidHits = 5;
        else if (choice == 12)
            l.euclidHits = 7;
        else if (choice == 13)
            l.euclidHits = 0;
        else if (choice == 20)
            l = {};
        else if (choice == 21) {
            for (auto& each : sc.lanes)
                each.midiNote = 0;
        }
        context_.model.setPatternBank(bank);
    };
    menu.show(this);
}

} // namespace cl::ui
