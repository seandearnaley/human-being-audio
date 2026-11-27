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
    enum class DragMode { none, paintOn, velocity, laneLength, slicerLength, laneNote };

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
        Rect btnClone, btnClear;
        Rect btnFill;
        std::array<Rect, 3> lengthPills{};
        int steps = 16; // columns in the drum grid
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

    Layout computeLayout(float w, float h, int steps) const;
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

    void drawHeader(visage::Canvas& canvas, const Layout& lay, const Visuals& vis, EngineMode mode);
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
    int hoverAction_ = -1; // 0 = Clone, 1 = Clear, 2 = Fill, 10..12 = length pills

    DragMode dragMode_ = DragMode::none;
    int dragLane_ = -1;
    int dragStep_ = -1;
    grid::DragGesture gesture_; // was the last press a click or a drag?
    int dragBaseVel_ = 96;
    int dragBaseNote_ = 36;
};

} // namespace cl::ui
