// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "theme.h"

#include <visage/ui.h>

#include <memory>
#include <string>

namespace dt::ui {

class Panel : public visage::Frame {
  public:
    Panel(Context& context, std::string title, unsigned accent);

  protected:
    void draw(visage::Canvas& canvas) override;

    Context& context_;
    std::string title_;
    unsigned accent_;
};

std::unique_ptr<visage::Frame> makeStagePanel(Context& context);
std::unique_ptr<visage::Frame> makeSculptPanel(Context& context);
std::unique_ptr<visage::Frame> makeAnatomyPanel(Context& context);
std::unique_ptr<visage::Frame> makeComposePanel(Context& context);

} // namespace dt::ui
