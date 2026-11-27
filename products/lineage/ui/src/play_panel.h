// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "panels.h"
#include "strata_art.h"

namespace ln::ui {

class PlayPanel final : public Panel {
  public:
    explicit PlayPanel(Context& context);
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
        bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    };

    int activeProgram() const;
    Rect tabRect(int index) const;
    Rect sheetRect() const;
    Rect artRect() const;
    int tabAt(float x, float y) const;
    void selectProgram(int index);
    void syncDockAccents();
    void updatePointer(float x, float y, bool shiftDown);
    void endPadTouch();

    Chip coreChip_, filt1Chip_, driveChip_, dirChip_;
    Row filt1Row_, driveRow_, genMixRow_, subRow_;

    StrataMotion motion_{};
    int program_ = 0;
    int programBefore_ = 0;
    int hoveredTab_ = -1;
    float change_ = 1.0f;
    float ripple_ = 1.0f;
    bool draggingPad_ = false;
    bool touching_ = false;
    bool fineDragging_ = false;
    float dragOriginX_ = 0.0f, dragOriginY_ = 0.0f;
    float dragStartX_ = 0.5f, dragStartY_ = 0.5f;
};

} // namespace ln::ui
