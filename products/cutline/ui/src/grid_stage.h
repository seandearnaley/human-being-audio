// SPDX-License-Identifier: AGPL-3.0-or-later
// The GRID stage: every bar of the edited scene as resolveBar plays it, or the fill bar, or the MIDI Slicer strip.
#pragma once

#include "grid_edit.h"
#include "panels.h"

#include <algorithm>
#include <array>
#include <functional>

namespace cl::ui {

class GridStagePanel final : public Panel {
  public:
    explicit GridStagePanel(Context& context);
    ~GridStagePanel() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void mouseMove(const visage::MouseEvent& e) override;
    void mouseExit(const visage::MouseEvent&) override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseDrag(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;

  private:
    // litPress: a press on a lit step that has not moved yet. Its first axis makes it erase or velocity; a release in
    // place turns the step off. nudge: an Option press on a lit step; a release in place toggles its flam.
    enum class DragMode { none, paintOn, erase, litPress, velocity, nudge, laneLength, slicerLength, laneNote };
    static constexpr int kSlicerRow = 99; // dragLane_ / hoverLane_ for the MIDI Slicer gate strip

    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        bool contains(float px, float py) const {
            return w > 0.0f && h > 0.0f && px >= x && px <= x + w && py >= y && py <= y + h;
        }
    };

    struct Layout {
        Rect btnClone, btnClear, btnVary;
        Rect btnFill;
        std::array<Rect, 3> lengthPills{};
        std::array<Rect, 3> brushChips{}; // Core, Groove, Detail
        int steps = 16;                   // columns in the drum grid
        float phraseY = 0.0f;             // the phrase strip, in drum mode; zero height in MIDI Slicer mode
        float phraseH = 0.0f;
        float rulerY = 0.0f;
        float gridX = 0.0f;
        float gridY = 0.0f;
        float gridW = 0.0f;
        float gridH = 0.0f;
        float rowH = 0.0f;
        float stepW = 0.0f;
        float slicerStepW = 0.0f;
        float sidebarX = 0.0f;
        float sidebarW = 0.0f;
        float footerY = 0.0f;
        // Slicer split rects
        Rect slicerVisRect{};
        Rect slicerGateRect{};
    };

    Layout computeLayout(float w, float h, int steps, bool drum) const;
    Layout layout() const; // for the current size, pattern and mode
    Rect laneNoteRect(int lane, const Layout& lay) const;
    Rect laneMuteRect(int lane, const Layout& lay) const;
    Rect laneSoloRect(int lane, const Layout& lay) const;
    float laneEndHandleX(int laneLen, const Layout& lay) const;
    float slicerEndHandleX(int length, const Layout& lay) const;
    // Half-width of a length handle's hit zone: narrow enough to leave the neighbouring cells clickable.
    float handleZone(float stepW) const { return std::min(px(7.0f), stepW * 0.25f); }

    int gridSteps() const;
    int editScene() const;
    bool fillView() const { return context_.fillView; }
    void edit(const std::function<bool(GrooveBank&)>& change);
    // One press, one undo step: the model takes one snapshot for every edit between these.
    void openGesture();
    void closeGesture();
    // The step a drag on the pressed lane (or the gate strip) is over, clamped to the grid; -1 when there is none.
    int dragColumn(float mx, const Layout& lay) const;
    // The step a press edits: a drum step of the edited scene, or a gate step when `lane` is kSlicerRow.
    Step* editableStep(GrooveBank& bank, int lane, int step) const;
    bool paintAt(GrooveBank& bank, int lane, int step) const;
    bool eraseAt(GrooveBank& bank, int lane, int step) const;

    void drawHeader(visage::Canvas& canvas, const Layout& lay, const Visuals& vis, EngineMode mode);
    void drawPhraseStrip(visage::Canvas& canvas, const Layout& lay, const Visuals& vis);
    void drawTicks(visage::Canvas& canvas, const grid::CellTicks& ticks, float x, float w, float y, float h, float dim);
    void drawDrumGrid(visage::Canvas& canvas, const Layout& lay, const Visuals& vis);
    void drawSlicerStage(visage::Canvas& canvas, const Layout& lay, const Visuals& vis);
    void showDrumStepMenu(int lane, int step);
    void showSlicerStepMenu(int step);
    void showLaneMenu(int lane);

    std::array<float, 5> activityHold_{};
    int hoverLane_ = -1;
    int hoverStep_ = -1;
    int hoverLengthLane_ = -1;
    bool hoverSlicerLen_ = false;
    int hoverAction_ = -1; // 0 = Clone, 1 = Clear, 2 = Fill, 3 = Vary, 10..12 = length pills, 20..22 = brush chips

    Layer brush_ = Layer::groove; // UI state: not saved with the plugin
    DragMode dragMode_ = DragMode::none;
    int dragLane_ = -1;
    int dragStep_ = -1;
    grid::DragGesture gesture_; // was the last press a click or a drag, and which way did it go first?
    bool gestureOpen_ = false;
    int dragBaseVel_ = 96;
    int dragBaseNote_ = 36;
    int dragBaseNudge_ = 0;
};

} // namespace cl::ui
