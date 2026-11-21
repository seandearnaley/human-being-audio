#include "tf/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <visage/app.h>

namespace tf::ui {

namespace {
// Design-space rectangles at 1280x720.
struct Rect {
    float x, y, width, height;
};
constexpr float kMargin = metric::margin;
constexpr float kContentWidth = kDefaultWidth - 2.0f * kMargin;
constexpr Rect kTopBar { kMargin, 12.0f, kContentWidth, 64.0f };
constexpr Rect kGraph { kMargin, 84.0f, kContentWidth, 292.0f };
constexpr Rect kAnalysis { kMargin, 388.0f, kContentWidth, 132.0f };
constexpr float kOutputWidth = 304.0f;
constexpr Rect kMovement { kMargin, 532.0f, kContentWidth - kOutputWidth - kMargin, 176.0f };
constexpr Rect kOutput { kDefaultWidth - kMargin - kOutputWidth, 532.0f, kOutputWidth, 176.0f };
}

struct Editor::Impl final : visage::EventTimer {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), topBar(context, std::move(showAbout)), graph(makeGraphPanel(context)), analysis(makeAnalysisPanel(context)),
          movement(makeMovementPanel(context)), output(makeOutputPanel(context)) {
        window.onDraw() = [this](visage::Canvas& canvas) {
            canvas.setColor(colour::window);
            canvas.fill(0, 0, window.width(), window.height());
        };
        // Add to Visage's own resize handler rather than replacing it.
        window.onResize() += [this] { layout(); };
        for (visage::Frame* child : children()) window.addChild(child);
    }

    std::array<visage::Frame*, 5> children() {
        return { &topBar, graph.get(), analysis.get(), movement.get(), output.get() };
    }

    void layout() {
        context.scale = window.width() / static_cast<float>(kDefaultWidth);
        const auto place = [this](visage::Frame& frame, const Rect& r) {
            frame.setBounds(context.px(r.x), context.px(r.y), context.px(r.width), context.px(r.height));
        };
        place(topBar, kTopBar);
        place(*graph, kGraph);
        place(*analysis, kAnalysis);
        place(*movement, kMovement);
        place(*output, kOutput);
        window.redrawAll();
    }

    void timerCallback() override {
        // Controls may add or remove themselves while ticking, so walk a copy.
        const auto live = context.live;
        for (Live* frame : live) frame->tick();
    }

    Context context;
    visage::ApplicationWindow window;
    TopBar topBar;
    std::unique_ptr<Panel> graph, analysis, movement, output;
};

Editor::Editor(Model& model, std::function<void()> showAbout) : impl_(std::make_unique<Impl>(model, std::move(showAbout))) {}

Editor::~Editor() { detach(); }

void Editor::attach(void* parentView, int width, int height) {
    impl_->window.setWindowDimensions(static_cast<float>(width), static_cast<float>(height));
    impl_->window.show(parentView);
    impl_->startTimer(16);
}

void Editor::detach() {
    impl_->stopTimer();
    if (impl_->window.isShowing()) impl_->window.close();
}

void Editor::setSize(int width, int height) { impl_->window.setWindowDimensionsFromHost(width, height); }

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->timerCallback();
    impl_->window.setWindowless(width, height);
    impl_->window.takeScreenshot().save(path);
}

}
