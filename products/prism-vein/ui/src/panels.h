// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "controls.h"

#include <memory>
#include <vector>

namespace pv::ui {

class Panel : public visage::Frame, public Live {
  public:
    explicit Panel(Context& context);
    ~Panel() override;

    void tick() override {}

  protected:
    template <typename Child, typename... Args> Child& add(Args&&... args) {
        auto child = std::make_unique<Child>(std::forward<Args>(args)...);
        Child& ref = *child;
        addChild(child.get());
        owned_.push_back(std::move(child));
        return ref;
    }

    float px(float designPixels) const { return context_.px(designPixels); }
    void drawChrome(visage::Canvas& canvas) const { drawPanel(canvas, context_, 0.0f, 0.0f, width(), height()); }

    Context& context_;

  private:
    std::vector<std::unique_ptr<visage::Frame>> owned_;
};

std::unique_ptr<visage::Frame> makeStagePanel(Context& context);
std::unique_ptr<visage::Frame> makeColorPanel(Context& context);
std::unique_ptr<visage::Frame> makeLatticePanel(Context& context);
std::unique_ptr<Panel> makeMovementPanel(Context& context);

} // namespace pv::ui
