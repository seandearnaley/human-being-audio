// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "theme.h"

#include <hba/ui/controls.h>

#include <functional>
#include <string>

namespace tg::ui {

using ParamFrame = hba::ui::ParamFrameBase<Context, ParamId, ParamSpec, ParamKind,
                                           hba::ui::ParamControlMetrics{170.0f, 0.12f, 0.015f}>;

class Knob final : public ParamFrame {
  public:
    using ModProvider = std::function<float()>;
    Knob(Context& ctx, ParamId id, unsigned accent, std::string shortLabel = {});
    void setModProvider(ModProvider provider) { modProvider_ = std::move(provider); }
    void tick() override;

  private:
    void paint(visage::Canvas& canvas) override;
    std::string label_;
    ModProvider modProvider_;
    float drawnMod_ = -1.0f;
};

class Row final : public ParamFrame {
  public:
    Row(Context& ctx, ParamId id, unsigned accent, std::string label = {}, bool bar = true);

  private:
    void paint(visage::Canvas& canvas) override;
    std::string label_;
    bool bar_;
};

class Chip final : public ParamFrame {
  public:
    Chip(Context& ctx, ParamId id, unsigned accent, std::string prefix = {});

  private:
    void paint(visage::Canvas& canvas) override;
    std::string prefix_;
};

class ActionButton final : public hba::ui::ActionButtonBase<Context> {
  public:
    using ActionButtonBase::ActionButtonBase;

    void draw(visage::Canvas& canvas) override;
};

} // namespace tg::ui
