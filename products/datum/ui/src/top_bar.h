// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "controls.h"
#include "dt/ui/editor.h"

#include <visage/widgets.h>

#include <functional>
#include <string>

namespace dt::ui {

class TopBar final : public visage::Frame, public Live {
  public:
    TopBar(Context& context, std::function<void()> showAbout, std::function<void(Editor::View)> onViewChanged);
    ~TopBar() override;

    void setView(Editor::View view);
    Editor::View view() const noexcept { return view_; }

    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void resized() override;

  private:
    void showPresetMenu();
    void drawWordmark(visage::Canvas& canvas);

    Context& context_;
    std::function<void(Editor::View)> onViewChanged_;
    Editor::View view_ = Editor::View::sculpt;

    visage::Button wordmark_;
    PillButton prevPreset_, presetName_, nextPreset_;
    PillButton undo_, redo_, ab_;
    PillButton viewSculpt_, viewAnatomy_, viewCompose_;
    PillButton hitBtn_, dragWav_, dragMid_;

    std::string shownPreset_;
    float peakDbL_ = -100.0f;
    float peakDbR_ = -100.0f;
    float grDb_ = 0.0f;
};

} // namespace dt::ui
