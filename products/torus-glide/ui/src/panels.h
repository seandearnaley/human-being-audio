// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "theme.h"

#include <visage/ui.h>

#include <memory>

namespace tg::ui {

class Panel : public visage::Frame {
  public:
    explicit Panel(Context& ctx) : context_(ctx) {}

  protected:
    Context& context_;
};

std::unique_ptr<Panel> makeStagePanel(Context& ctx);
std::unique_ptr<Panel> makeControlsStrip(Context& ctx);
std::unique_ptr<Panel> makeMovementPanel(Context& ctx);

} // namespace tg::ui
