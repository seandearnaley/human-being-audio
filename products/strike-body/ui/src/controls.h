// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "theme.h"

#include <hba/ui/controls.h>

#include <functional>
#include <string>

namespace sb::ui {

using ParamFrame = hba::ui::ParamFrameBase<Context, ParamId, ParamSpec, ParamKind,
                                           hba::ui::ParamControlMetrics{165.0f, 0.12f, 0.015f}>;

class Knob final : public ParamFrame {
  public:
    Knob(Context& ctx, ParamId id, unsigned accent, std::string shortLabel = {}, bool hero = false);

  private:
    void paint(visage::Canvas& canvas) override;
    std::string label_;
    bool hero_;
};

// Sleek tactile filled-pill slider, choice chip, or compact toggle badge!
class Row final : public ParamFrame {
  public:
    enum class Style { fader, chip, badge };
    Row(Context& ctx, ParamId id, unsigned accent, std::string label = {}, Style style = Style::fader);

  private:
    void paint(visage::Canvas& canvas) override;
    std::string label_;
    Style style_;
};

class ActionButton final : public hba::ui::ActionButtonBase<Context> {
  public:
    using ActionButtonBase::ActionButtonBase;

    void setGradientAccent(unsigned fromArgb, unsigned toArgb) {
        accent_ = fromArgb;
        gradEnd_ = toArgb;
        redraw();
    }

    void draw(visage::Canvas& canvas) override;

  private:
    unsigned gradEnd_ = 0u;
};

} // namespace sb::ui
