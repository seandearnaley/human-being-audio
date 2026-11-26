// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "controls.h"

#include <visage/widgets.h>

#include <array>
#include <functional>
#include <string>

namespace cl::ui {

class TopBar final : public visage::Frame, public Live {
  public:
    TopBar(Context& context, std::function<void()> showAbout, std::function<void(bool)> onSurfaceChange);
    ~TopBar() override;

    void setGridMode(bool grid);

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void resized() override;
    void mouseMove(const visage::MouseEvent& e) override;
    void mouseExit(const visage::MouseEvent& e) override;
    void mouseDown(const visage::MouseEvent& e) override;

  private:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    };

    void drawWordmark(visage::Canvas& canvas);
    void showPresetMenu();
    void computeRightRects(std::array<Rect, 3>& rates, std::array<Rect, kSceneCount>& scenes, Rect& drumRect,
                           Rect& slicerRect, Rect& playRect, Rect& gridRect) const;

    Context& context_;
    std::function<void(bool)> onSurfaceChange_;
    visage::Button wordmark_;
    Button previous_;
    Button name_;
    Button next_;
    Button save_;
    Button undo_;
    Button redo_;
    Button ab_;
    std::string shownPreset_;
    bool gridMode_ = false;
    int hoveredTag_ = -1; // 0..3 scenes, 10=drum, 11=slicer, 20=play, 21=grid, 30..32=rates
};

} // namespace cl::ui
