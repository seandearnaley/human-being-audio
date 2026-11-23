// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/ui/editor.h"

#include "panels.h"
#include "top_bar.h"

#include <visage/app.h>

#include <array>

namespace cl::ui {

namespace {
struct Rect {
    float x, y, width, height;
};
constexpr float kMargin = metric::margin;
constexpr float kContentWidth = kDefaultWidth - 2.0f * kMargin;
constexpr Rect kTopBar{kMargin, 12.0f, kContentWidth, 64.0f};
constexpr Rect kMainDeck{kMargin, 88.0f, kContentWidth, 432.0f};
constexpr float kDeliveryWidth = 316.0f;
constexpr Rect kMovement{kMargin, 532.0f, kContentWidth - kDeliveryWidth - kMargin, 176.0f};
constexpr Rect kDelivery{kDefaultWidth - kMargin - kDeliveryWidth, 532.0f, kDeliveryWidth, 176.0f};
} // namespace

struct Editor::Impl final : visage::EventTimer {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), topBar(context, std::move(showAbout)), phrase(makePhrasePanel(context)),
          movement(makeMovementPanel(context)), delivery(makeDeliveryPanel(context)) {
        window.onDraw() = [this](visage::Canvas& canvas) {
            canvas.setColor(colour::window);
            canvas.fill(0, 0, window.width(), window.height());
        };
        window.onResize() += [this] { layout(); };
        window.setAcceptsKeystrokes(true);
        window.onKeyPress() = [this](const visage::KeyEvent& key) {
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
        for (visage::Frame* child : children())
            window.addChild(child);
    }

    std::array<visage::Frame*, 4> children() { return {&topBar, phrase.get(), movement.get(), delivery.get()}; }

    void layout() {
        context.scale = window.width() / static_cast<float>(kDefaultWidth);
        const auto place = [this](visage::Frame& frame, const Rect& r) {
            frame.setBounds(context.px(r.x), context.px(r.y), context.px(r.width), context.px(r.height));
        };
        place(topBar, kTopBar);
        place(*phrase, kMainDeck);
        place(*movement, kMovement);
        place(*delivery, kDelivery);
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
    std::unique_ptr<Panel> phrase, movement, delivery;
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
    auto* window = impl_->window.window();
    return window != nullptr ? window->posixFd() : -1;
}

void Editor::dispatchEvents() {
    if (auto* window = impl_->window.window())
        window->processPluginFdEvents();
}
#endif

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->timerCallback();
    impl_->window.setWindowless(width, height);
    impl_->window.takeScreenshot().save(path);
}

} // namespace cl::ui
