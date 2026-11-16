#include "panels.h"

namespace tf::ui {

namespace {
class AnalysisPanel final : public Panel {
public:
    explicit AnalysisPanel(Context& context) : Panel(context) {}

    void draw(visage::Canvas& canvas) override {
        drawChrome(canvas);
        drawLabel(canvas, context_, "Analysis", px(metric::pad), px(metric::pad), width(), px(16.0f), colour::textDim,
                  visage::Font::kLeft, type::title);
    }
};
}

std::unique_ptr<Panel> makeAnalysisPanel(Context& context) { return std::make_unique<AnalysisPanel>(context); }

}
