// SPDX-License-Identifier: GPL-3.0-or-later
#include "lc/ui/editor.h"

#include "classic_panel.h"
#include "pad_panel.h"
#include "phrase_tape.h"
#include "top_bar.h"

#include <visage/app.h>

namespace lc::ui {

namespace {
struct Rect {
    float x, y, width, height;
};
constexpr float kMargin = metric::margin;
constexpr float kContentWidth = kDefaultWidth - 2.0f * kMargin;
constexpr Rect kTopBar{kMargin, 12.0f, kContentWidth, 52.0f};
constexpr Rect kPhraseTape{kMargin, 72.0f, kContentWidth, 52.0f};
constexpr Rect kMainDeck{kMargin, 134.0f, kContentWidth, 574.0f};
} // namespace

struct Editor::Impl final : visage::EventTimer {
    Impl(Model& model, std::function<void()> showAbout)
        : context(model), topBar(context, std::move(showAbout)), tape(context), pad(context), classic(context) {
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

        window.addChild(&topBar);
        window.addChild(&tape);
        window.addChild(&pad);
        window.addChild(&classic);
        syncSurface();
    }

    void syncSurface() {
        const bool isClassic = context.model.getNormalised("surface") >= 0.5f;
        if (isClassic == shownClassic && initialisedSurface)
            return;
        initialisedSurface = true;
        shownClassic = isClassic;
        pad.setVisible(!isClassic);
        classic.setVisible(isClassic);
        window.redrawAll();
    }

    void layout() {
        context.scale = window.width() / static_cast<float>(kDefaultWidth);
        const auto place = [this](visage::Frame& frame, const Rect& r) {
            frame.setBounds(context.px(r.x), context.px(r.y), context.px(r.width), context.px(r.height));
        };
        place(topBar, kTopBar);
        place(tape, kPhraseTape);
        place(pad, kMainDeck);
        place(classic, kMainDeck);
        window.redrawAll();
    }

    void timerCallback() override {
        context.model.refreshVisuals();
        syncSurface();
        const auto live = context.live;
        for (Live* frame : live)
            frame->tick();
    }

    Context context;
    visage::ApplicationWindow window;
    TopBar topBar;
    PhraseTape tape;
    PadPanel pad;
    ClassicPanel classic;
    bool shownClassic = false;
    bool initialisedSurface = false;
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

void Editor::selectClassicTab(int tabIndex) { impl_->classic.selectTab(tabIndex); }

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

} // namespace lc::ui
