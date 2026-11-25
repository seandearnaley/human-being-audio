// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "theme.h"

#include <visage/ui.h>

#include <memory>

namespace sb::ui {

class Panel : public visage::Frame {
  public:
    explicit Panel(Context& ctx) : context_(ctx) {}

  protected:
    Context& context_;
};

std::unique_ptr<Panel> makeStagePanel(Context& ctx);
std::unique_ptr<Panel> makeMacroPanel(Context& ctx);
std::unique_ptr<Panel> makeRackPanel(Context& ctx);

} // namespace sb::ui
