#include "panels.h"

namespace tf::ui {

namespace {
class GraphPanel final : public Panel {
public:
    explicit GraphPanel(Context& context) : Panel(context) {}

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        drawLabel(canvas, context_, "Graph", px(metric::pad), px(metric::pad), width(), px(16.0f), colour::textDim,
                  visage::Font::kLeft, type::title);
    }
};
}

std::unique_ptr<Panel> makeGraphPanel(Context& context) { return std::make_unique<GraphPanel>(context); }

}
