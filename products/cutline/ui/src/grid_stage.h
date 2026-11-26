// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "panels.h"

#include <array>

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
    enum class DragMode { none, paintOn, paintOff, velocity, laneLength, slicerLength, laneNote };

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
        Rect modeDrum, modeSlicer;
        std::array<Rect, kSceneCount> scenes{};
        Rect btnMutate, btnPoly, btnClone, btnClear;
        float rulerY = 0.0f;
        float gridX = 0.0f;
        float gridY = 0.0f;
        float gridW = 0.0f;
        float gridH = 0.0f;
        float rowH = 0.0f;
        float stepW = 0.0f;
        float sidebarX = 0.0f;
        float sidebarW = 0.0f;
        float footerY = 0.0f;
        // Slicer split rects
        Rect slicerVisRect{};
        Rect slicerGateRect{};
    };

    Layout computeLayout(float w, float h) const;
    Rect laneNoteRect(int lane, const Layout& lay) const;
    Rect laneMuteRect(int lane, const Layout& lay) const;
    Rect laneSoloRect(int lane, const Layout& lay) const;
    Rect laneLockRect(int lane, const Layout& lay) const;
    float laneEndHandleX(int laneLen, const Layout& lay) const;

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
    int hoverScene_ = -1;
    int hoverAction_ = -1; // 0=Mutate, 1=Poly, 2=Clone, 3=Clear

    DragMode dragMode_ = DragMode::none;
    int dragLane_ = -1;
    int dragStep_ = -1;
    float dragStartY_ = 0.0f;
    int dragBaseVelOff_ = 0;
    int dragBaseNote_ = 36;
};

} // namespace cl::ui
