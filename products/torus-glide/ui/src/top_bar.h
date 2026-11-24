// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "controls.h"

#include <visage/widgets.h>

#include <functional>
#include <string>

namespace tg::ui {

class TopBar final : public visage::Frame, public Live {
  public:
    TopBar(Context& ctx, std::function<void()> showAbout);
    ~TopBar() override;

    void tick() override;
    void resized() override;
    void draw(visage::Canvas& canvas) override;

  private:
    void showPresetMenu();
    void drawWordmark(visage::Canvas& canvas);
    void setMode(int idx);

    Context& context_;
    visage::Button wordmark_;
    ActionButton modeSpiral_;
    ActionButton modeCopedent_;
    ActionButton modeMobius_;
    ActionButton prevPreset_;
    ActionButton presetName_;
    ActionButton nextPreset_;
    ActionButton undo_;
    ActionButton redo_;
    ActionButton ab_;
    Chip keyChip_;
    Chip scaleChip_;
    ActionButton audition_;
    ActionButton dragWav_;
    ActionButton dragMid_;

    std::string shownPreset_;
    int shownMode_ = -1;
};

} // namespace tg::ui
