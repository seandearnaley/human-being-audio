// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "controls.h"

#include <memory>
#include <vector>

namespace cl::ui {

class Panel : public visage::Frame, public Live {
  public:
    explicit Panel(Context& context);
    ~Panel() override;

    void tick() override {}
    void draw(visage::Canvas& canvas) override { drawChrome(canvas); }

  protected:
    template <typename T, typename... Args> T& add(Args&&... args) {
        auto control = std::make_unique<T>(std::forward<Args>(args)...);
        T& reference = *control;
        addChild(control.get());
        owned_.push_back(std::move(control));
        return reference;
    }

    float px(float designPixels) const { return context_.px(designPixels); }
    void drawChrome(visage::Canvas& canvas) { drawPanel(canvas, context_, 0.0f, 0.0f, width(), height()); }

    Context& context_;

  private:
    std::vector<std::unique_ptr<visage::Frame>> owned_;
};

std::unique_ptr<Panel> makeMacroStripPanel(Context& context);
std::unique_ptr<Panel> makeGridStagePanel(Context& context);
std::unique_ptr<Panel> makeSetupPanel(Context& context);
std::unique_ptr<Panel> makeMovementPanel(Context& context);
std::unique_ptr<Panel> makeDeliveryPanel(Context& context);

} // namespace cl::ui
