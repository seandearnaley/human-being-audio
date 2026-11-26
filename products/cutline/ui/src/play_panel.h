// SPDX-License-Identifier: AGPL-3.0-or-later
// Cutline 2.0 Hero PLAY Surface: 8 Jewel Folder Tabs (C3..G3) + Giant 60fps Generative 2D X/Y Articulation Stage
// + Sleek Bottom Performance Dock.
#pragma once

#include "controls.h"
#include "pad_art.h"

#include <string>

namespace cl::ui {

class PlayPanel final : public visage::Frame, public Live {
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
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    };

    int playingProgram() const;
    Rect tabRect(int index) const;
    Rect sheetRect() const;
    Rect artRect() const;
    int tabAt(float x, float y) const;
    std::string liveStatusBadge(int prog, float x, float y, bool engaged, bool holdOn) const;
    std::string formatAxisValue(int prog, bool isX, float v) const;

    void updatePointer(float x, float y, bool shiftDown);
    void endPadTouch();
    void toggleHold();
    void selectProgram(int index);

    Context& context_;

    // Bottom Play Dock Controls
    Button holdBtn_;
    Chip quantChip_;
    Chip styleChip_;
    Button mutateBtn_;
    Button polyBtn_;
    Chip ccChip_;
    Chip noteMapChip_;
    Chip slicerChip_;
    Button auditionBtn_;
    Button dragMidBtn_;
    std::string shownExport_;

    // Live Animation & Touch State
    int program_ = 4;
    int programBefore_ = 4;
    float change_ = 1.0f;
    float ring_ = 1.0f;
    int hoveredTab_ = -1;
    bool draggingPad_ = false;
    bool touching_ = false;
    bool fineDragging_ = false;
    float dragOriginX_ = 0.0f;
    float dragOriginY_ = 0.0f;
    float dragStartX_ = 0.5f;
    float dragStartY_ = 0.5f;
    double lastPlayhead_ = 0.0;
    PadMotion motion_{};
};

} // namespace cl::ui
