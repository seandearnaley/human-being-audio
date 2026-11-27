// SPDX-License-Identifier: AGPL-3.0-or-later
// Compact live 8-lane pulse ribbon (what resolveBar plays) + Slicer chord telemetry bar above the PLAY XY stage.
#pragma once

#include "controls.h"

#include <array>
#include <functional>

namespace cl::ui {

class PulseRibbon final : public visage::Frame, public Live {
  public:
    PulseRibbon(Context& context, std::function<void()> onOpenGrid);
    ~PulseRibbon() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void mouseEnter(const visage::MouseEvent&) override;
    void mouseExit(const visage::MouseEvent&) override;
    void mouseDown(const visage::MouseEvent& e) override;

  private:
    Context& context_;
    std::function<void()> onOpenGrid_;
    std::array<float, 3> ledHold_{}; // NOTE, CC, XY
    bool hoveredTimeline_ = false;
};

} // namespace cl::ui
