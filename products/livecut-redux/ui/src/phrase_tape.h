// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "theme.h"

#include <visage/ui.h>

namespace lc::ui {

// Live incoming audio waveform + active slice/onset markers + phrase block & playhead tape.
class PhraseTape final : public visage::Frame, public Live {
  public:
    explicit PhraseTape(Context& context);
    ~PhraseTape() override;

    void tick() override;
    void draw(visage::Canvas& canvas) override;

  private:
    Context& context_;
    double lastPos_ = -1.0;
    double lastBlockStart_ = -1.0;
    int lastProgram_ = -1;
    std::string lastHeadline_;
};

} // namespace lc::ui
