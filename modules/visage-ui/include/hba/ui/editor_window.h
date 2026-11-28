// SPDX-License-Identifier: GPL-3.0-or-later
// Shared Visage ApplicationWindow + 60fps EventTimer host for studio editors.
#pragma once

#include <hba/ui/theme.h>
#include <visage/app.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace hba::ui {

struct DesignRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

template <typename ContextT> class StudioEditorWindow final : public visage::EventTimer {
  public:
    StudioEditorWindow(ContextT& context, float defaultWidth) : context_(context), defaultWidth_(defaultWidth) {
        window_.onDraw() = [this](visage::Canvas& canvas) {
            if (onDrawBackground) {
                onDrawBackground(canvas, window_.width(), window_.height());
            } else {
                canvas.setColor(colour::window);
                canvas.fill(0, 0, window_.width(), window_.height());
            }
        };
        // Add to Visage's own resize handler rather than replacing it.
        window_.onResize() += [this] { layout(); };
        window_.setAcceptsKeystrokes(true);
        window_.onKeyPress() = [this](const visage::KeyEvent& key) {
            if (onKeyPress && onKeyPress(key))
                return true;
            if (key.keyCode() == visage::KeyCode::Tab && !key.isMainModifier()) {
                std::vector<visage::Frame*> controls;
                const auto visit = [&](auto& self, visage::Frame& frame) -> void {
                    for (auto* child : frame.children()) {
                        if (!child->isVisible())
                            continue;
                        if (child->acceptsKeystrokes())
                            controls.push_back(child);
                        self(self, *child);
                    }
                };
                visit(visit, window_);
                if (controls.empty())
                    return false;
                auto focus =
                    std::find_if(controls.begin(), controls.end(), [](auto* f) { return f->hasKeyboardFocus(); });
                if (key.isShiftDown())
                    focus = focus == controls.begin() || focus == controls.end() ? controls.end() - 1 : focus - 1;
                else if (focus == controls.end() || ++focus == controls.end())
                    focus = controls.begin();
                (*focus)->requestKeyboardFocus();
                return true;
            }
            if (!key.isMainModifier())
                return false;
            if (key.keyCode() == visage::KeyCode::Z) {
                if (key.isShiftDown())
                    context_.model.redo();
                else
                    context_.model.undo();
                return true;
            }
            if (key.keyCode() == visage::KeyCode::Y) {
                context_.model.redo();
                return true;
            }
            return false;
        };
    }

    ~StudioEditorWindow() override { detach(); }

    void addPlaced(visage::Frame* child, DesignRect rect) {
        placed_.push_back({child, rect});
        window_.addChild(child);
    }

    void layout() {
        context_.scale = window_.width() / defaultWidth_;
        for (const auto& [frame, r] : placed_) {
            frame->setBounds(context_.px(r.x), context_.px(r.y), context_.px(r.width), context_.px(r.height));
        }
        window_.redrawAll();
    }

    void timerCallback() override {
        context_.model.refreshVisuals();
        if (onBeforeLiveTick)
            onBeforeLiveTick();
        // Controls may add or remove themselves while ticking, so walk a copy
        // and verify each control is still registered before invoking tick().
        const auto live = context_.live;
        for (Live* frame : live) {
            if (std::find(context_.live.begin(), context_.live.end(), frame) != context_.live.end())
                frame->tick();
        }
    }

    void attach(void* parentView, int width, int height) {
#if defined(_WIN32)
        window_.show(visage::Dimension::nativePixels(static_cast<float>(width)),
                     visage::Dimension::nativePixels(static_cast<float>(height)), parentView);
#else
        window_.setWindowDimensions(static_cast<float>(width), static_cast<float>(height));
        window_.show(parentView);
#endif
        startTimer(16);
    }

    void detach() {
        stopTimer();
        if (window_.isShowing())
            window_.close();
    }

    void setSize(int width, int height) {
#if defined(_WIN32)
        window_.setNativeWindowDimensionsFromHost(width, height);
#else
        window_.setWindowDimensionsFromHost(width, height);
#endif
    }

#if defined(__linux__)
    int eventFd() const {
        auto* w = window_.window();
        return w != nullptr ? w->posixFd() : -1;
    }

    void dispatchEvents() {
        if (auto* w = window_.window())
            w->processPluginFdEvents();
    }
#endif

    void renderToPng(const std::string& path, int width, int height, bool relayoutAfterWindowless = false) {
        timerCallback();
        if (relayoutAfterWindowless) {
            window_.setWindowDimensions(static_cast<float>(width), static_cast<float>(height));
            window_.setWindowless(width, height);
            layout();
            timerCallback();
        } else {
            window_.setWindowless(width, height);
        }
        window_.takeScreenshot().save(path);
    }

    visage::ApplicationWindow& window() { return window_; }

    std::function<void(visage::Canvas&, float, float)> onDrawBackground;
    std::function<bool(const visage::KeyEvent&)> onKeyPress;
    std::function<void()> onBeforeLiveTick;

  private:
    ContextT& context_;
    float defaultWidth_ = 1280.0f;
    visage::ApplicationWindow window_;
    std::vector<std::pair<visage::Frame*, DesignRect>> placed_;
};

} // namespace hba::ui
