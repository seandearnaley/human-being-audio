// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "controls.h"
#include "pv/ui/editor.h"

#include <visage/widgets.h>

#include <functional>
#include <string>

namespace pv::ui {

class TopBar final : public visage::Frame, public Live {
  public:
    TopBar(Context& context, std::function<void()> showAbout, std::function<void(Editor::View)> onViewChanged);
    ~TopBar() override;

    void setView(Editor::View view);
    void tick() override;
    void draw(visage::Canvas& canvas) override;
    void resized() override;

  private:
    void showPresetMenu();
    void drawWordmark(visage::Canvas& canvas);

    Context& context_;
    std::function<void(Editor::View)> onViewChanged_;
    Editor::View view_ = Editor::View::color;

    visage::Button wordmark_;
    PillButton prevPreset_;
    PillButton presetName_;
    PillButton nextPreset_;
    PillButton undo_;
    PillButton redo_;
    PillButton ab_;
    PillButton viewColor_;
    PillButton viewLattice_;
    PillButton freezeBtn_;
    PillButton holdBtn_;
    PillButton demoBtn_;

    std::string shownPreset_;
    float peakDbL_ = -100.0f;
    float peakDbR_ = -100.0f;
    float grDb_ = 0.0f;
};

} // namespace pv::ui
