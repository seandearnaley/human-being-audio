// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <visage/app.h>

#include <array>

namespace tg::ui {

namespace {
struct Rect {
    float x, y, w, h;
};
constexpr float kM = metric::margin;
constexpr float kW = kDefaultWidth - 2.0f * kM;
constexpr Rect kTopBar{kM, 10.0f, kW, 56.0f};
constexpr Rect kStage{kM, 74.0f, kW, 286.0f};
constexpr Rect kControls{kM, 368.0f, kW, 224.0f};
constexpr Rect kMovement{kM, 600.0f, kW, 150.0f};
} // namespace

struct Editor::Impl final : visage::EventTimer {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), topBar(context, std::move(showAbout)), stage(makeStagePanel(context)),
          controls(makeControlsStrip(context)), movement(makeMovementPanel(context)) {
        window.onDraw() = [this](visage::Canvas& canvas) {
            canvas.setColor(colour::window);
            canvas.fill(0, 0, window.width(), window.height());
        };
        window.onResize() += [this] { layout(); };
        window.setAcceptsKeystrokes(true);
        window.onKeyPress() = [this](const visage::KeyEvent& key) {
            if (key.keyCode() == visage::KeyCode::Space && !key.isMainModifier()) {
                context.model.setAudition(!context.model.isAuditioning());
                return true;
            }
            if (!key.isMainModifier())
                return false;
            if (key.keyCode() == visage::KeyCode::Z) {
                if (key.isShiftDown())
                    context.model.redo();
                else
                    context.model.undo();
                return true;
            }
            if (key.keyCode() == visage::KeyCode::Y) {
                context.model.redo();
                return true;
            }
            return false;
        };

        for (visage::Frame* child : std::array<visage::Frame*, 4>{&topBar, stage.get(), controls.get(), movement.get()})
            window.addChild(child);
    }

    void layout() {
        context.scale = window.width() / static_cast<float>(kDefaultWidth);
        const auto place = [this](visage::Frame& f, const Rect& r) {
            f.setBounds(context.px(r.x), context.px(r.y), context.px(r.w), context.px(r.h));
        };
        place(topBar, kTopBar);
        place(*stage, kStage);
        place(*controls, kControls);
        place(*movement, kMovement);
        window.redrawAll();
    }

    void timerCallback() override {
        context.model.refreshVisuals();
        const auto live = context.live;
        for (Live* frame : live)
            frame->tick();
    }

    Context context;
    visage::ApplicationWindow window;
    TopBar topBar;
    std::unique_ptr<Panel> stage, controls, movement;
};

Editor::Editor(Model& model, std::function<void()> showAbout)
    : impl_(std::make_unique<Impl>(model, std::move(showAbout))) {}

Editor::~Editor() { detach(); }

void Editor::attach(void* parentView, int width, int height) {
    impl_->window.setWindowDimensions(static_cast<float>(width), static_cast<float>(height));
    impl_->window.show(parentView);
    impl_->startTimer(16);
}

void Editor::detach() {
    impl_->stopTimer();
    if (impl_->window.isShowing())
        impl_->window.close();
}

void Editor::setSize(int width, int height) { impl_->window.setWindowDimensionsFromHost(width, height); }

#if defined(__linux__)
int Editor::eventFd() const {
    auto* w = impl_->window.window();
    return w != nullptr ? w->posixFd() : -1;
}
void Editor::dispatchEvents() {
    if (auto* w = impl_->window.window())
        w->processPluginFdEvents();
}
#endif

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->timerCallback();
    impl_->window.setWindowDimensions(static_cast<float>(width), static_cast<float>(height));
    impl_->window.setWindowless(width, height);
    impl_->layout();
    impl_->timerCallback();
    impl_->window.takeScreenshot().save(path);
}

} // namespace tg::ui
