// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "controls.h"
#include "manifold_art.h"
#include "panels.h"

namespace tg::ui {

class PlayPanel final : public Panel, public Live {
  public:
    explicit PlayPanel(Context& ctx);
    ~PlayPanel() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void resized() override;
    void mouseMove(const visage::MouseEvent& e) override;
    void mouseExit(const visage::MouseEvent& e) override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseDrag(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;
    bool keyPress(const visage::KeyEvent& key) override;

  private:
    struct Rect {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        bool contains(float px, float py) const {
            return w > 0.0f && h > 0.0f && px >= x && px <= x + w && py >= y && py <= y + h;
        }
    };

    Rect tabRect(int index) const;
    Rect sheetRect() const;
    Rect artRect() const;
    int tabAt(float x, float y) const;
    int activeProgram() const;
    void selectProgram(int index);
    void updatePointer(float x, float y, bool shiftDown);
    void endPadTouch();
    void syncDockVisibility();

    // Bottom Play Dock controls
    Chip modeChip_;
    Chip timbreChip_;
    Chip loopChip_;
    Chip stackChip_;
    Chip voicingChip_;
    Row filterRow_;
    Row driveRow_;
    Row subRow_;
    Row spaceRow_;

    int program_ = 0;
    int programBefore_ = 0;
    int hoveredTab_ = -1;
    int shownMode_ = -1;
    float change_ = 1.0f;
    float ripple_ = 1.0f;
    ManifoldMotion motion_{};

    bool draggingPad_ = false;
    bool touching_ = false;
    bool fineDragging_ = false;
    float dragOriginX_ = 0.0f;
    float dragOriginY_ = 0.0f;
    float dragStartX_ = 0.5f;
    float dragStartY_ = 0.5f;
};

} // namespace tg::ui
