#include "panels.h"

namespace tf::ui {

namespace {
class OutputPanel final : public Panel {
public:
    explicit OutputPanel(Context& context) : Panel(context) {}

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        drawLabel(canvas, context_, "Output", px(metric::pad), px(metric::pad), width(), px(16.0f), colour::textDim,
                  visage::Font::kLeft, type::title);
    }
};
}

std::unique_ptr<Panel> makeOutputPanel(Context& context) { return std::make_unique<OutputPanel>(context); }

}
