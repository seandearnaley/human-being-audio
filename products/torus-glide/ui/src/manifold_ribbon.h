// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "panels.h"

namespace tg::ui {

class ManifoldRibbon final : public Panel, public Live {
  public:
    explicit ManifoldRibbon(Context& ctx);
    ~ManifoldRibbon() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void mouseDown(const visage::MouseEvent& e) override;

  private:
    float pulse_ = 0.0f;
};

} // namespace tg::ui
