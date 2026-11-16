#include "panels.h"

namespace tf::ui {

namespace {
class MovementPanel final : public Panel {
public:
    explicit MovementPanel(Context& context) : Panel(context) {}

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        drawLabel(canvas, context_, "Movement", px(metric::pad), px(metric::pad), width(), px(16.0f), colour::textDim,
                  visage::Font::kLeft, type::title);
    }
};
}

std::unique_ptr<Panel> makeMovementPanel(Context& context) { return std::make_unique<MovementPanel>(context); }

}
