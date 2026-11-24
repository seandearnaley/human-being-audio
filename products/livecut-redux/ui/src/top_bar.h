// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "controls.h"
#include "theme.h"

#include <visage/ui.h>
#include <visage/widgets.h>

#include <functional>
#include <string>

namespace lc::ui {

class TopBar final : public visage::Frame, public Live {
  public:
    TopBar(Context& context, std::function<void()> showAbout);
    ~TopBar() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void resized() override;

  private:
    void showPresetMenu();

    Context& context_;
    visage::Button wordmark_;
    ActionButton previous_;
    ActionButton presetName_;
    ActionButton next_;
    ActionButton save_;
    ActionButton undo_;
    ActionButton redo_;
    ActionButton ab_;
    ParamSegmented surface_;
    ParamSlider subAnchor_;
    TogglePill limiter_;

    std::string shownPreset_;
    float peakL_ = 0.0f;
    float peakR_ = 0.0f;
    float grDb_ = 0.0f;
};

} // namespace lc::ui
