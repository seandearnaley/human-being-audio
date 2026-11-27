// SPDX-License-Identifier: AGPL-3.0-or-later
#include "grid_stage.h"

#include "cl/vary.h"

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

void GridStagePanel::openGesture() {
    closeGesture(); // a press whose release never arrived
    context_.model.beginGrooveEdit();
    gestureOpen_ = true;
}

void GridStagePanel::closeGesture() {
    if (!gestureOpen_)
        return;
    gestureOpen_ = false;
    context_.model.endGrooveEdit();
}

GridStagePanel::Layout GridStagePanel::layout() const {
    return computeLayout(width(), height(), gridSteps(), engineMode(context_.model) == EngineMode::drum);
}

int GridStagePanel::dragColumn(float mx, const Layout& lay) const {
    const bool slicer = dragLane_ == kSlicerRow;
    const float x0 = slicer ? lay.slicerGateRect.x : lay.gridX;
    const float stepW = slicer ? lay.slicerStepW : lay.stepW;
    const int columns = slicer ? kMaxSlicerSteps : lay.steps;
    if (stepW <= 0.0f || columns <= 0)
        return -1;
    return std::clamp(static_cast<int>(std::floor((mx - x0) / stepW)), 0, columns - 1);
}

Step* GridStagePanel::editableStep(GrooveBank& bank, int lane, int step) const {
    if (lane == kSlicerRow)
        return (step >= 0 && step < static_cast<int>(bank.slicerLength))
                   ? &bank.slicerSteps[static_cast<std::size_t>(step)]
                   : nullptr;
    return grid::stepAt(bank.scenes[static_cast<std::size_t>(editScene())], fillView(), lane, step);
}

bool GridStagePanel::paintAt(GrooveBank& bank, int lane, int step) const {
    if (lane != kSlicerRow)
        return grid::paintStep(bank.scenes[static_cast<std::size_t>(editScene())], fillView(), lane, step, brush_);
    Step* s = editableStep(bank, lane, step);
    if (s == nullptr || s->on())
        return false;
    *s = slicerHit();
    return true;
}

bool GridStagePanel::eraseAt(GrooveBank& bank, int lane, int step) const {
    Step* s = editableStep(bank, lane, step);
    if (s == nullptr || !s->on())
        return false;
    *s = {};
    return true;
}

void GridStagePanel::mouseMove(const visage::MouseEvent& e) {
    const float mx = e.position.x;
    const float my = e.position.y;
    const Layout lay = layout();
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
    else if (drum && !fillView() && lay.btnVary.contains(mx, my))
        hoverAction_ = 3;
    else if (drum && lay.btnLibrary.contains(mx, my))
        hoverAction_ = 4;
    for (std::size_t i = 0; drum && i < lay.lengthPills.size(); ++i)
        if (lay.lengthPills[i].contains(mx, my))
            hoverAction_ = 10 + static_cast<int>(i);
    for (std::size_t i = 0; drum && !fillView() && i < lay.brushChips.size(); ++i)
        if (lay.brushChips[i].contains(mx, my))
            hoverAction_ = 20 + static_cast<int>(i);

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
                hoverLane_ = kSlicerRow;
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
    const Layout lay = layout();
    const bool drum = engineMode(context_.model) == EngineMode::drum;
    const int scene = editScene();
    const auto si = static_cast<std::size_t>(scene);

    // A new press starts from a clean drag state, even if the last release never arrived.
    closeGesture();
    dragMode_ = DragMode::none;
    dragLane_ = -1;
    dragStep_ = -1;
    gesture_.begin(mx, my);

    // 1. Header actions. Each is its own undo step.
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
            openGesture();
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
        const Step& pressed = bank.slicerSteps[st];
        if (e.isAltDown()) { // Option: drag to nudge, click to toggle the chord strum
            if (!pressed.on())
                return;
            openGesture();
            dragMode_ = DragMode::nudge;
            dragLane_ = kSlicerRow;
            dragStep_ = step;
            dragBaseNudge_ = static_cast<int>(pressed.nudge);
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
        openGesture();
        dragLane_ = kSlicerRow;
        dragStep_ = step;
        if (pressed.on()) {
            dragMode_ = DragMode::litPress;
            dragBaseVel_ = static_cast<int>(pressed.velocity);
        } else {
            dragMode_ = DragMode::paintOn;
            edit([&](GrooveBank& b) { return paintAt(b, kSlicerRow, step); });
        }
        return;
    }

    if (lay.btnFill.contains(mx, my)) {
        context_.fillView = !context_.fillView;
        redraw();
        return;
    }

    if (lay.btnLibrary.contains(mx, my)) {
        showLibraryMenu();
        return;
    }

    if (!fillView()) {
        // Vary: a variation of the edited scene's pattern, as one undo step.
        if (lay.btnVary.contains(mx, my)) {
            edit([&](GrooveBank& b) {
                Pattern varied;
                if (!vary(b.scenes[si], varied))
                    return false;
                b.scenes[si] = varied;
                return true;
            });
            return;
        }
        // The layer brush: UI state only.
        for (std::size_t i = 0; i < lay.brushChips.size(); ++i) {
            if (lay.brushChips[i].contains(mx, my)) {
                brush_ = static_cast<Layer>(i);
                redraw();
                return;
            }
        }
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
        openGesture();
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
        openGesture();
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
    if (e.isAltDown()) { // Option: drag a lit step to nudge it, click it to toggle its flam
        if (!target->on())
            return;
        openGesture();
        dragMode_ = DragMode::nudge;
        dragLane_ = lane;
        dragStep_ = step;
        dragBaseNudge_ = static_cast<int>(target->nudge);
        return;
    }
    if (e.isShiftDown()) {
        const Layer brush = brush_;
        edit([&](GrooveBank& b) {
            Pattern& p = b.scenes[si];
            Step* s = grid::stepAt(p, fill, lane, step);
            if (s == nullptr)
                return false;
            if (!s->on())
                grid::paintStep(p, fill, lane, step, brush);
            return grid::cycleRatchet(*s);
        });
        return;
    }
    openGesture();
    dragLane_ = lane;
    dragStep_ = step;
    if (target->on()) {
        dragMode_ = DragMode::litPress; // erase or velocity, decided by the first axis it moves along
        dragBaseVel_ = static_cast<int>(target->velocity);
    } else {
        dragMode_ = DragMode::paintOn;
        edit([&](GrooveBank& b) { return paintAt(b, lane, step); });
    }
}

void GridStagePanel::mouseDrag(const visage::MouseEvent& e) {
    if (dragMode_ == DragMode::none)
        return;

    const float mx = e.position.x;
    const float my = e.position.y;
    const Layout lay = layout();
    const auto si = static_cast<std::size_t>(editScene());

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

    if (dragMode_ == DragMode::nudge) {
        if (!gesture_.move(mx, my, px(4.0f)))
            return; // still a click: its release toggles the flam
        const int nudge = grid::nudgeFromDrag(dragBaseNudge_, mx - gesture_.startX(), px(grid::kNudgeDragPx));
        edit([&](GrooveBank& b) {
            Step* s = editableStep(b, dragLane_, dragStep_);
            return s != nullptr && grid::setStepNudge(*s, nudge);
        });
        return;
    }

    if (dragMode_ == DragMode::litPress) {
        if (!gesture_.move(mx, my, px(4.0f)))
            return;
        if (gesture_.axis() == grid::Axis::horizontal) {
            // Erase: the pressed step goes off now, and every lit step the pointer passes on this lane after it.
            dragMode_ = DragMode::erase;
            edit([&](GrooveBank& b) { return eraseAt(b, dragLane_, dragStep_); });
        } else {
            dragMode_ = DragMode::velocity;
        }
    }

    if (dragMode_ == DragMode::velocity) {
        const float dy = gesture_.startY() - my;
        const int velocity = dragBaseVel_ + static_cast<int>(std::lround(dy * 1.4f));
        edit([&](GrooveBank& b) {
            Step* s = editableStep(b, dragLane_, dragStep_);
            return s != nullptr && grid::setStepVelocity(*s, velocity);
        });
        return;
    }

    if (dragMode_ == DragMode::paintOn || dragMode_ == DragMode::erase) {
        // Paint and erase stay on the pressed lane, and touch every step between the last position and this one, so
        // a fast drag misses none.
        const int step = dragColumn(mx, lay);
        if (step < 0 || step == dragStep_ || dragStep_ < 0)
            return;
        const int from = dragStep_;
        dragStep_ = step;
        const bool paint = dragMode_ == DragMode::paintOn;
        const int dir = step > from ? 1 : -1;
        edit([&](GrooveBank& b) {
            bool changed = false;
            for (int st = from + dir;; st += dir) {
                changed = (paint ? paintAt(b, dragLane_, st) : eraseAt(b, dragLane_, st)) || changed;
                if (st == step)
                    break;
            }
            return changed;
        });
    }
}

void GridStagePanel::mouseUp(const visage::MouseEvent& e) {
    // A press released in place: on a lit step it switches the step off; with Option it toggles the flam. A drag never
    // does either, even one that returns to where it started.
    const bool click = !gesture_.move(e.position.x, e.position.y, px(4.0f));
    if (click && (dragMode_ == DragMode::litPress || dragMode_ == DragMode::nudge)) {
        const bool flam = dragMode_ == DragMode::nudge;
        const int lane = dragLane_;
        const int step = dragStep_;
        edit([&](GrooveBank& b) {
            Step* s = editableStep(b, lane, step);
            if (s == nullptr || !s->on())
                return false; // already off, perhaps cleared by the host: never light it
            if (flam)
                return grid::toggleFlam(*s);
            *s = {};
            return true;
        });
    }
    dragMode_ = DragMode::none;
    dragLane_ = -1;
    dragStep_ = -1;
    closeGesture();
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
    const Layer brush = brush_;
    menu.onSelection() += [this, lane, step, fill, brush](int choice) {
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
            if (!s->on()) // every other choice needs a lit step: light it as a new step, with the brush
                grid::paintStep(p, fill, lane, step, brush);
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

// The factory library, one submenu per category. A choice replaces the edited scene's pattern (pattern view) or its
// fill (fill view), as one undo step.
void GridStagePanel::showLibraryMenu() {
    const bool fill = fillView();
    visage::PopupMenu menu;
    auto addGrouped = [&menu](const auto& items) {
        visage::PopupMenu group;
        std::string category;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i == 0 || items[i].category != category) {
                if (i > 0)
                    menu.addSubMenu(std::move(group));
                category = items[i].category;
                group = visage::PopupMenu(category);
            }
            group.addOption(static_cast<int>(i) + 1, items[i].name);
        }
        if (!items.empty())
            menu.addSubMenu(std::move(group));
    };
    if (fill)
        addGrouped(context_.model.fillLibrary());
    else
        addGrouped(context_.model.patternLibrary());
    if (menu.size() == 0)
        return;
    menu.onSelection() += [this, fill](int choice) {
        const auto index = static_cast<std::size_t>(choice - 1);
        const auto scene = static_cast<std::size_t>(editScene());
        edit([&](GrooveBank& bank) {
            if (fill) {
                const auto& fills = context_.model.fillLibrary();
                if (index >= fills.size())
                    return false;
                bank.scenes[scene].fill = fills[index].fill;
                return true;
            }
            const auto& patterns = context_.model.patternLibrary();
            if (index >= patterns.size())
                return false;
            bank.scenes[scene] = patterns[index].pattern;
            return true;
        });
    };
    menu.show(this);
}

void GridStagePanel::showLaneMenu(int lane) {
    constexpr std::array<const char*, grid::kPocketChoicesMs.size()> kPocketNames{
        "Pocket: Early 10 ms", "Pocket: Early 5 ms", "Pocket: On Grid", "Pocket: Late 5 ms", "Pocket: Late 10 ms"};
    const int pocket = static_cast<int>(context_.model.groove()
                                            .bank.scenes[static_cast<std::size_t>(editScene())]
                                            .lanes[static_cast<std::size_t>(lane)]
                                            .offsetMs);
    visage::PopupMenu menu;
    menu.addOption(1, "Length: Follow Pattern");
    menu.addOption(2, "Length: 5 steps");
    menu.addOption(3, "Length: 7 steps");
    menu.addOption(4, "Length: 12 steps");
    menu.addBreak();
    for (std::size_t i = 0; i < kPocketNames.size(); ++i)
        menu.addOption(30 + static_cast<int>(i), kPocketNames[i]).select(pocket == grid::kPocketChoicesMs[i]);
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
                if (choice >= 30 && choice < 30 + static_cast<int>(grid::kPocketChoicesMs.size()))
                    return grid::setLanePocket(p, lane, grid::kPocketChoicesMs[static_cast<std::size_t>(choice - 30)]);
                return false;
            }
        });
    };
    menu.show(this);
}

} // namespace cl::ui
