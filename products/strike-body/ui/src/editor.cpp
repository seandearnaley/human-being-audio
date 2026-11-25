// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sb/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <visage/app.h>

#include <array>

namespace sb::ui {

namespace {
struct Rect {
    float x, y, w, h;
};
constexpr float kM = metric::margin;
constexpr float kW = kDefaultWidth - 2.0f * kM;
constexpr Rect kTopBar{kM, 10.0f, kW, 54.0f};
constexpr Rect kStage{kM, 72.0f, kW, 246.0f};
constexpr Rect kMacro{kM, 326.0f, kW, 114.0f};
constexpr Rect kRack{kM, 448.0f, kW, 362.0f};
} // namespace

struct Editor::Impl final : visage::EventTimer {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), topBar(context, std::move(showAbout)), stage(makeStagePanel(context)),
          macro(makeMacroPanel(context)), rack(makeRackPanel(context)) {
        window.onDraw() = [this](visage::Canvas& canvas) {
            canvas.setBrush(visage::Brush::vertical(colour::obsidianBgTop, colour::obsidianBgBot));
            canvas.fill(0, 0, window.width(), window.height());
        };
        window.onResize() += [this] { layout(); };
        window.setAcceptsKeystrokes(true);
        window.onKeyPress() = [this](const visage::KeyEvent& key) {
            if (!key.isMainModifier()) {
                if (key.keyCode() == visage::KeyCode::Space || key.keyCode() == visage::KeyCode::Number1) {
                    context.model.triggerAudition(Articulation::main, 1.0f);
                    return true;
                }
                if (key.keyCode() == visage::KeyCode::Number2) {
                    context.model.triggerAudition(Articulation::ghost, 0.72f);
                    return true;
                }
                if (key.keyCode() == visage::KeyCode::Number3) {
                    context.model.triggerAudition(Articulation::rim, 0.96f);
                    return true;
                }
                if (key.keyCode() == visage::KeyCode::Number4) {
                    context.model.triggerAudition(Articulation::roll, 0.92f);
                    return true;
                }
                if (key.keyCode() == visage::KeyCode::M) {
                    context.model.mutateSound();
                    return true;
                }
                return false;
            }
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

        for (visage::Frame* child : std::array<visage::Frame*, 4>{&topBar, stage.get(), macro.get(), rack.get()})
            window.addChild(child);
    }

    void layout() {
        context.scale = window.width() / static_cast<float>(kDefaultWidth);
        const auto place = [this](visage::Frame& f, const Rect& r) {
            f.setBounds(context.px(r.x), context.px(r.y), context.px(r.w), context.px(r.h));
        };
        place(topBar, kTopBar);
        place(*stage, kStage);
        place(*macro, kMacro);
        place(*rack, kRack);
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
    std::unique_ptr<Panel> stage, macro, rack;
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

} // namespace sb::ui
