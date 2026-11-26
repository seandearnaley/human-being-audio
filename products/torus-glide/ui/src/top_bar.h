// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "controls.h"

#include <visage/widgets.h>

#include <functional>
#include <string>

namespace tg::ui {

class TopBar final : public visage::Frame, public Live {
  public:
    TopBar(Context& ctx, std::function<void()> showAbout, std::function<void(bool)> onViewChanged);
    ~TopBar() override;

    void setStudioView(bool studio);
    void tick() override;
    void resized() override;
    void draw(visage::Canvas& canvas) override;

  private:
    void showPresetMenu();
    void drawWordmark(visage::Canvas& canvas);

    Context& context_;
    std::function<void(bool)> onViewChanged_;
    visage::Button wordmark_;
    ActionButton prevPreset_;
    ActionButton presetName_;
    ActionButton nextPreset_;
    ActionButton undo_;
    ActionButton redo_;
    ActionButton ab_;
    Chip keyChip_;
    Chip scaleChip_;
    ActionButton drone_;
    ActionButton dragWav_;
    ActionButton dragMid_;
    ActionButton viewPlay_;
    ActionButton viewStudio_;

    std::string shownPreset_;
    bool studioView_ = false;
    float peakDbL_ = -96.0f;
    float peakDbR_ = -96.0f;
    float grDb_ = 0.0f;
};

} // namespace tg::ui
