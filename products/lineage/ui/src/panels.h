// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "controls.h"

#include <memory>
#include <vector>

namespace ln::ui {

class Panel : public visage::Frame, public Live {
  public:
    explicit Panel(Context& context);
    ~Panel() override;

    void tick() override {}

  protected:
    template <class Child, class... Args> Child& add(Args&&... args) {
        auto child = std::make_unique<Child>(std::forward<Args>(args)...);
        auto* raw = child.get();
        addChild(raw);
        owned_.push_back(std::move(child));
        return *raw;
    }

    float px(float designPixels) const { return context_.px(designPixels); }
    void drawChrome(visage::Canvas& canvas) const { drawPanel(canvas, context_, 0.0f, 0.0f, width(), height()); }

    Context& context_;

  private:
    std::vector<std::unique_ptr<visage::Frame>> owned_;
};

Params paramsOf(const Model& model);
LaneCurves curvesOf(const Model& model);

std::unique_ptr<Panel> makeStrataRibbon(Context& context);
std::unique_ptr<Panel> makePlayPanel(Context& context);
std::unique_ptr<Panel> makeStudioStage(Context& context);
std::unique_ptr<Panel> makeStudioDeck(Context& context);
std::unique_ptr<Panel> makeMovementPanel(Context& context);

} // namespace ln::ui
