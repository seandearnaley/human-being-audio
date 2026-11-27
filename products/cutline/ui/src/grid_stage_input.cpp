// SPDX-License-Identifier: AGPL-3.0-or-later
#include "grid_stage.h"

#include <visage/ui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cl::ui {

namespace {

constexpr std::array<int, 3> kLengthValues{1, 2, 4};

EngineMode engineMode(const Model& model) {
    return static_cast<EngineMode>(std::clamp(static_cast<int>(model.get(ParamId::engineMode) + 0.5f), 0, 1));
}

grid::Geometry drumGeometry(float gridX, float gridY, float stepW, float rowH, int steps) {
    return {gridX, gridY, stepW, rowH, steps};
}

// A lit slicer step, as a click or a menu choice makes it.
Step slicerHit() {
    Step s;
    s.velocity = 96;
    return s;
}

} // namespace

void GridStagePanel::edit(const std::function<bool(GrooveBank&)>& change) {
    GrooveBank bank = context_.model.groove().bank;
    if (change(bank))
        context_.model.setGrooveBank(bank);
}

void GridStagePanel::mouseMove(const visage::MouseEvent& e) {
    const float mx = e.position.x;
    const float my = e.position.y;
    const Layout lay = computeLayout(width(), height(), gridSteps());
    const bool drum = engineMode(context_.model) == EngineMode::drum;

    hoverLane_ = -1;
    hoverStep_ = -1;
    hoverLengthLane_ = -1;
    hoverSlicerLen_ = false;
    hoverAction_ = -1;

    if (lay.btnClone.contains(mx, my))
        hoverAction_ = 0;
    else if (lay.btnClear.contains(mx, my))
        hoverAction_ = 1;
    else if (drum && lay.btnFill.contains(mx, my))
        hoverAction_ = 2;
    for (std::size_t i = 0; drum && i < lay.lengthPills.size(); ++i)
        if (lay.lengthPills[i].contains(mx, my))
            hoverAction_ = 10 + static_cast<int>(i);

    if (drum) {
        const grid::Geometry geometry = drumGeometry(lay.gridX, lay.gridY, lay.stepW, lay.rowH, lay.steps);
        int lane = -1;
        int step = -1;
        // The row under the mouse, wherever it is across the row: the loop handle can sit right of the last column.
        if (!fillView() && grid::cellAt(geometry, lay.gridX, my, lane, step)) {
            const Pattern& pattern = context_.model.groove().bank.scenes[static_cast<std::size_t>(editScene())];
            if (std::abs(mx - laneEndHandleX(laneLoopSteps(pattern, lane), lay)) <= handleZone(lay.stepW)) {
                hoverLengthLane_ = lane;
                redraw();
                return;
            }
        }
        if (grid::cellAt(geometry, mx, my, lane, step)) {
            hoverLane_ = lane;
            hoverStep_ = step;
        }
    } else {
        const auto& gr = lay.slicerGateRect;
        if (my >= gr.y && my <= gr.y + gr.h) {
            const int slLen =
                std::clamp(static_cast<int>(context_.model.groove().bank.slicerLength), 1, kMaxSlicerSteps);
            int row = -1;
            int step = -1;
            if (std::abs(mx - slicerEndHandleX(slLen, lay)) <= handleZone(lay.slicerStepW)) {
                hoverSlicerLen_ = true;
            } else if (grid::cellAt({gr.x, gr.y, lay.slicerStepW, gr.h, kMaxSlicerSteps, 1}, mx, my, row, step)) {
                hoverLane_ = 99;
                hoverStep_ = step;
            }
        }
    }
    redraw();
}

void GridStagePanel::mouseExit(const visage::MouseEvent&) {
    hoverLane_ = hoverStep_ = hoverLengthLane_ = hoverAction_ = -1;
    hoverSlicerLen_ = false;
    redraw();
}

void GridStagePanel::mouseDown(const visage::MouseEvent& e) {
    const float mx = e.position.x;
    const float my = e.position.y;
    const Layout lay = computeLayout(width(), height(), gridSteps());
    const bool drum = engineMode(context_.model) == EngineMode::drum;
    const int scene = editScene();
    const auto si = static_cast<std::size_t>(scene);

    // A new press starts from a clean drag state, even if the last release never arrived.
    dragMode_ = DragMode::none;
    dragLane_ = -1;
    dragStep_ = -1;
    gesture_.begin(my);

    // 1. Header actions.
    if (lay.btnClone.contains(mx, my)) {
        context_.model.cloneActiveSceneToNext();
        return;
    }
    if (lay.btnClear.contains(mx, my)) {
        context_.model.clearActiveScene();
        return;
    }

    if (!drum) {
        // MIDI Slicer: the gate strip.
        const auto& gr = lay.slicerGateRect;
        if (my < gr.y || my > gr.y + gr.h)
            return;
        const GrooveBank& bank = context_.model.groove().bank;
        const int slLen = std::clamp(static_cast<int>(bank.slicerLength), 1, kMaxSlicerSteps);
        if (std::abs(mx - slicerEndHandleX(slLen, lay)) <= handleZone(lay.slicerStepW)) {
            dragMode_ = DragMode::slicerLength;
            return;
        }
        int row = -1;
        int step = -1;
        if (!grid::cellAt({gr.x, gr.y, lay.slicerStepW, gr.h, kMaxSlicerSteps, 1}, mx, my, row, step) || step >= slLen)
            return;
        const auto st = static_cast<std::size_t>(step);
        if (e.shouldTriggerPopup()) {
            showSlicerStepMenu(step);
            return;
        }
        if (e.isShiftDown()) {
            edit([&](GrooveBank& b) {
                Step& s = b.slicerSteps[st];
                if (!s.on())
                    s = slicerHit();
                return grid::cycleRatchet(s);
            });
            return;
        }
        dragLane_ = 99;
        dragStep_ = step;
        if (bank.slicerSteps[st].on()) {
            dragMode_ = DragMode::velocity;
            dragBaseVel_ = static_cast<int>(bank.slicerSteps[st].velocity);
        } else {
            dragMode_ = DragMode::paintOn;
            edit([&](GrooveBank& b) {
                b.slicerSteps[st] = slicerHit();
                return true;
            });
        }
        return;
    }

    if (lay.btnFill.contains(mx, my)) {
        context_.fillView = !context_.fillView;
        redraw();
        return;
    }

    // 2. Length pills: bars in the pattern view, fill beats in the fill view.
    for (std::size_t i = 0; i < lay.lengthPills.size(); ++i) {
        if (!lay.lengthPills[i].contains(mx, my))
            continue;
        const int n = kLengthValues[i];
        const bool fill = fillView();
        edit([&](GrooveBank& b) {
            Pattern& p = b.scenes[si];
            if (fill)
                return grid::setFillBeats(p, n);
            if (patternBars(p) == n)
                return false;
            setPatternBars(p, n);
            return true;
        });
        return;
    }

    const grid::Geometry geometry = drumGeometry(lay.gridX, lay.gridY, lay.stepW, lay.rowH, lay.steps);
    int lane = -1;
    int step = -1;
    if (!grid::cellAt(geometry, lay.gridX, my, lane, step)) // the row under the mouse
        return;
    const auto li = static_cast<std::size_t>(lane);
    const Pattern& pattern = context_.model.groove().bank.scenes[si];

    // 3. Mute and solo.
    if (laneMuteRect(lane, lay).contains(mx, my)) {
        edit([&](GrooveBank& b) {
            b.scenes[si].lanes[li].muted = !b.scenes[si].lanes[li].muted;
            return true;
        });
        return;
    }
    if (laneSoloRect(lane, lay).contains(mx, my)) {
        edit([&](GrooveBank& b) {
            b.scenes[si].lanes[li].solo = !b.scenes[si].lanes[li].solo;
            return true;
        });
        return;
    }

    // 4. The note badge: right-click returns to the kit map's note; a vertical drag picks another.
    if (laneNoteRect(lane, lay).contains(mx, my)) {
        if (e.shouldTriggerPopup()) {
            edit([&](GrooveBank& b) {
                if (b.scenes[si].lanes[li].midiNote == 0)
                    return false;
                b.scenes[si].lanes[li].midiNote = 0;
                return true;
            });
            return;
        }
        const auto noteMap = static_cast<NoteMap>(
            std::clamp(static_cast<int>(context_.model.get(ParamId::noteMap) + 0.5f), 0, kNoteMapCount - 1));
        dragMode_ = DragMode::laneNote;
        dragLane_ = lane;
        dragBaseNote_ = effectiveLaneMidiNote(noteMap, lane, pattern.lanes[li].midiNote);
        return;
    }

    // 5. Elsewhere in the sidebar.
    if (mx < lay.gridX) {
        if (e.shouldTriggerPopup())
            showLaneMenu(lane);
        return;
    }

    // 6. The loop-length handle, in the pattern view.
    if (!fillView() && std::abs(mx - laneEndHandleX(laneLoopSteps(pattern, lane), lay)) <= handleZone(lay.stepW)) {
        dragMode_ = DragMode::laneLength;
        dragLane_ = lane;
        return;
    }

    // 7. The cells.
    if (!grid::cellAt(geometry, mx, my, lane, step))
        return;
    const Step* target = grid::stepAt(pattern, fillView(), lane, step);
    if (target == nullptr)
        return;
    if (e.shouldTriggerPopup()) {
        showDrumStepMenu(lane, step);
        return;
    }
    const bool fill = fillView();
    if (e.isShiftDown()) {
        edit([&](GrooveBank& b) {
            Pattern& p = b.scenes[si];
            Step* s = grid::stepAt(p, fill, lane, step);
            if (s == nullptr)
                return false;
            if (!s->on())
                grid::toggleStep(p, fill, lane, step);
            return grid::cycleRatchet(*s);
        });
        return;
    }
    dragLane_ = lane;
    dragStep_ = step;
    if (target->on()) {
        dragMode_ = DragMode::velocity;
        dragBaseVel_ = static_cast<int>(target->velocity);
    } else {
        dragMode_ = DragMode::paintOn;
        edit([&](GrooveBank& b) { return grid::toggleStep(b.scenes[si], fill, lane, step); });
    }
}

void GridStagePanel::mouseDrag(const visage::MouseEvent& e) {
    if (dragMode_ == DragMode::none)
        return;

    const float mx = e.position.x;
    const float my = e.position.y;
    const Layout lay = computeLayout(width(), height(), gridSteps());
    const auto si = static_cast<std::size_t>(editScene());
    const bool fill = fillView();

    if (dragMode_ == DragMode::laneLength && dragLane_ >= 0 && dragLane_ < kDrumLaneCount) {
        const int steps = static_cast<int>(std::lround((mx - lay.gridX) / lay.stepW));
        edit([&](GrooveBank& b) { return grid::setLaneLength(b.scenes[si], dragLane_, steps); });
        return;
    }

    if (dragMode_ == DragMode::slicerLength) {
        const auto next = static_cast<std::uint8_t>(
            std::clamp(static_cast<int>(std::lround((mx - lay.gridX) / lay.slicerStepW)), 1, kMaxSlicerSteps));
        edit([&](GrooveBank& b) {
            if (b.slicerLength == next)
                return false;
            b.slicerLength = next;
            return true;
        });
        return;
    }

    if (dragMode_ == DragMode::laneNote && dragLane_ >= 0 && dragLane_ < kDrumLaneCount) {
        const int delta = static_cast<int>(std::lround((gesture_.startY() - my) / px(5.0f)));
        const auto next = static_cast<std::uint8_t>(std::clamp(dragBaseNote_ + delta, 24, 96));
        edit([&](GrooveBank& b) {
            std::uint8_t& note = b.scenes[si].lanes[static_cast<std::size_t>(dragLane_)].midiNote;
            if (note == next)
                return false;
            note = next;
            return true;
        });
        return;
    }

    if (dragMode_ == DragMode::velocity) {
        if (!gesture_.move(my, px(4.0f)))
            return;
        const float dy = gesture_.startY() - my;
        const int velocity = dragBaseVel_ + static_cast<int>(std::lround(dy * 1.4f));
        edit([&](GrooveBank& b) {
            if (dragLane_ == 99)
                return grid::setStepVelocity(b.slicerSteps[static_cast<std::size_t>(dragStep_)], velocity);
            Step* s = grid::stepAt(b.scenes[si], fill, dragLane_, dragStep_);
            return s != nullptr && grid::setStepVelocity(*s, velocity);
        });
        return;
    }

    if (dragMode_ == DragMode::paintOn) {
        int lane = -1;
        int step = -1;
        if (dragLane_ == 99) {
            const auto& gr = lay.slicerGateRect;
            if (!grid::cellAt({gr.x, gr.y, lay.slicerStepW, gr.h, kMaxSlicerSteps, 1}, mx, gr.y, lane, step))
                return;
        } else if (!grid::cellAt(drumGeometry(lay.gridX, lay.gridY, lay.stepW, lay.rowH, lay.steps), mx, my, lane,
                                 step)) {
            return;
        }
        if (step == dragStep_)
            return;
        dragStep_ = step;
        edit([&](GrooveBank& b) {
            if (dragLane_ == 99) {
                if (step >= static_cast<int>(b.slicerLength))
                    return false;
                Step& s = b.slicerSteps[static_cast<std::size_t>(step)];
                if (s.on())
                    return false;
                s = slicerHit();
                return true;
            }
            const Step* s = grid::stepAt(b.scenes[si], fill, dragLane_, step);
            return s != nullptr && !s->on() && grid::toggleStep(b.scenes[si], fill, dragLane_, step);
        });
    }
}

void GridStagePanel::mouseUp(const visage::MouseEvent& e) {
    // A click on a lit step switches it off. A drag never does, even one that returns to where it started.
    if (dragMode_ == DragMode::velocity && !gesture_.move(e.position.y, px(4.0f))) {
        const auto si = static_cast<std::size_t>(editScene());
        const bool fill = fillView();
        const int lane = dragLane_;
        const int step = dragStep_;
        edit([&](GrooveBank& b) {
            Step* s = lane == 99 ? &b.slicerSteps[static_cast<std::size_t>(step)]
                                 : grid::stepAt(b.scenes[si], fill, lane, step);
            if (s == nullptr || !s->on())
                return false; // already off, perhaps cleared by the host: never light it
            *s = {};
            return true;
        });
    }
    dragMode_ = DragMode::none;
    dragLane_ = -1;
    dragStep_ = -1;
}

void GridStagePanel::showDrumStepMenu(int lane, int step) {
    const bool fill = fillView();
    visage::PopupMenu menu;
    if (!fill) { // a fill step has no layer: if it is drawn, it sounds
        menu.addOption(20, "Layer: Core");
        menu.addOption(21, "Layer: Groove");
        menu.addOption(22, "Layer: Detail");
        menu.addBreak();
    }
    menu.addOption(1, "Ratchet: Off");
    menu.addOption(2, "Ratchet: \u00d72");
    menu.addOption(3, "Ratchet: \u00d73");
    menu.addOption(4, "Ratchet: \u00d74");
    menu.addBreak();
    menu.addOption(10, "Toggle Flam");
    menu.addOption(11, "Nudge Early");
    menu.addOption(12, "Nudge Late");
    menu.addOption(13, "Clear Step");
    menu.onSelection() += [this, lane, step, fill](int choice) {
        const int scene = editScene();
        edit([&](GrooveBank& bank) {
            Pattern& p = bank.scenes[static_cast<std::size_t>(scene)];
            Step* s = grid::stepAt(p, fill, lane, step);
            if (s == nullptr)
                return false;
            if (choice == 13) {
                *s = {};
                return true;
            }
            if (!s->on()) // every other choice needs a lit step: light it as a new step
                grid::toggleStep(p, fill, lane, step);
            if (choice >= 1 && choice <= 4)
                s->ratchet = static_cast<std::uint8_t>(choice);
            else if (choice == 10)
                s->flam = !s->flam;
            else if (choice == 11)
                s->nudge = static_cast<std::int8_t>(std::clamp(static_cast<int>(s->nudge) - 6, -24, 24));
            else if (choice == 12)
                s->nudge = static_cast<std::int8_t>(std::clamp(static_cast<int>(s->nudge) + 6, -24, 24));
            else if (choice >= 20 && choice <= 22)
                s->layer = static_cast<Layer>(choice - 20);
            return true;
        });
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
    menu.addOption(13, "Clear Step");
    menu.onSelection() += [this, step](int choice) {
        edit([&](GrooveBank& bank) {
            if (step < 0 || step >= static_cast<int>(bank.slicerLength))
                return false;
            Step& s = bank.slicerSteps[static_cast<std::size_t>(step)];
            if (choice == 13) {
                s = {};
                return true;
            }
            if (!s.on()) // every other choice needs a lit step: light it as a new step
                s = slicerHit();
            if (choice >= 1 && choice <= 4)
                s.ratchet = static_cast<std::uint8_t>(choice);
            else if (choice == 10)
                s.flam = !s.flam;
            return true;
        });
    };
    menu.show(this);
}

void GridStagePanel::showLaneMenu(int lane) {
    visage::PopupMenu menu;
    menu.addOption(1, "Length: Follow Pattern");
    menu.addOption(2, "Length: 5 steps");
    menu.addOption(3, "Length: 7 steps");
    menu.addOption(4, "Length: 12 steps");
    menu.addBreak();
    menu.addOption(20, "Reset Lane");
    menu.addOption(21, "Reset All Notes to Kit Map");
    menu.onSelection() += [this, lane](int choice) {
        const int scene = editScene();
        edit([&](GrooveBank& bank) {
            Pattern& p = bank.scenes[static_cast<std::size_t>(scene)];
            DrumLane& l = p.lanes[static_cast<std::size_t>(lane)];
            switch (choice) {
            case 1:
                if (l.length == 0)
                    return false;
                l.length = 0;
                return true;
            case 2:
                return grid::setLaneLength(p, lane, 5);
            case 3:
                return grid::setLaneLength(p, lane, 7);
            case 4:
                return grid::setLaneLength(p, lane, 12);
            case 20:
                l = DrumLane{};
                return true;
            case 21:
                for (auto& each : p.lanes)
                    each.midiNote = 0;
                return true;
            default:
                return false;
            }
        });
    };
    menu.show(this);
}

} // namespace cl::ui
