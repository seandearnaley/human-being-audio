// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "controls.h"
#include "pad_art.h"
#include "theme.h"

#include <visage/ui.h>

#include <array>
#include <optional>
#include <string>

namespace lc::ui {

class PadPanel final : public visage::Frame, public Live {
  public:
    explicit PadPanel(Context& context);
    ~PadPanel() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void resized() override;
    void mouseDown(const visage::MouseEvent& e) override;
    void mouseDrag(const visage::MouseEvent& e) override;
    void mouseUp(const visage::MouseEvent& e) override;
    void mouseMove(const visage::MouseEvent& e) override;
    void mouseExit(const visage::MouseEvent&) override;
    bool keyPress(const visage::KeyEvent& key) override;

  private:
    struct Rect {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    };

    Rect tabRect(int index) const;
    Rect sheetRect() const;
    Rect artRect() const;
    int tabAt(float x, float y) const;
    void updatePointer(float x, float y, bool shiftDown);
    void endPadTouch();
    int playingProgram() const;

    Context& context_;

    // Bottom performance dock controls
    TogglePill hold_;
    TogglePill straight_;
    ParamSegmented quantize_;
    ActionButton rollSeed_;
    ActionButton dragWav_;
    ParamSlider trim_;
    ParamSlider mix_;

    // Animated GPU Pad state
    ArtMotion motion_{};
    int program_ = 0;
    int programBefore_ = 0;
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
    std::optional<double> lastBlockStart_;
    double lastBeat_ = 0.0;
};

} // namespace lc::ui
