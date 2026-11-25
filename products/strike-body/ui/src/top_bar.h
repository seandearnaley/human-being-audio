// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "controls.h"

#include <visage/widgets.h>

#include <functional>
#include <string>

namespace sb::ui {

class TopBar final : public visage::Frame, public Live {
  public:
    TopBar(Context& ctx, std::function<void()> showAbout);
    ~TopBar() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void resized() override;

  private:
    void showPresetMenu();
    void drawWordmark(visage::Canvas& canvas);

    Context& context_;
    visage::Button wordmark_;
    ActionButton padMain_, padGhost_, padRim_, padRoll_;
    ActionButton prevPreset_, presetName_, nextPreset_;
    ActionButton undo_, redo_, ab_;
    ActionButton autoAlign_, bounceWav_;

    std::string shownPreset_;
    int flashArt_ = -1;
    int flashTicks_ = 0;
    std::uint32_t lastTrigCount_ = 0;
};

} // namespace sb::ui
