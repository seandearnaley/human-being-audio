// The four panels under the top bar. Each owns its controls and draws its own
// chrome; the editor only positions them.
#pragma once

#include "controls.h"

#include <memory>
#include <vector>

namespace tf::ui {

// Base for a panel: chrome, a place to keep child controls alive, and a tick.
class Panel : public visage::Frame, public Live {
public:
    explicit Panel(Context& context);
    ~Panel() override;

    void tick() override {}

protected:
    // Creates a control, keeps it alive and adds it as a child.
    template<typename T, typename... Args>
    T& add(Args&&... args) {
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

std::unique_ptr<Panel> makeGraphPanel(Context& context);
std::unique_ptr<Panel> makeAnalysisPanel(Context& context);
std::unique_ptr<Panel> makeMovementPanel(Context& context);
std::unique_ptr<Panel> makeOutputPanel(Context& context);

}
