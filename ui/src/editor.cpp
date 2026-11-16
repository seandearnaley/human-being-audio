#include "tf/ui/editor.h"

#include <embedded/tf_fonts.h>
#include <visage/app.h>

#include <cmath>

namespace tf::ui {

struct Editor::Impl {
    visage::ApplicationWindow window;

    Impl() {
        window.onDraw() = [this](visage::Canvas& canvas) { draw(canvas); };
    }

    void draw(visage::Canvas& canvas) {
        const float w = window.width();
        const float h = window.height();

        canvas.setColor(0xff0b0e13);
        canvas.fill(0, 0, w, h);

        const float radius = h * 0.18f;
        const float sweep = 1.2f + 0.9f * std::sin(static_cast<float>(canvas.time()) * 1.5f);
        canvas.setColor(0xff1a222c);
        canvas.ring(w * 0.5f - radius, h * 0.5f - radius, 2.0f * radius, 6.0f);
        canvas.setColor(0xff35d6e6);
        canvas.roundedArc(w * 0.5f - radius, h * 0.5f - radius, 2.0f * radius, 6.0f,
                          static_cast<float>(canvas.time()) * 2.0f, sweep);

        canvas.setColor(0xffe8edf2);
        const visage::Font font(28.0f, fonts::Inter_Medium_ttf);
        canvas.text("Tract Form", font, visage::Font::kCenter, 0, h * 0.5f - 20.0f, w, 40.0f);

        window.redraw();
    }
};

Editor::Editor() : impl_(std::make_unique<Impl>()) {}

Editor::~Editor() { detach(); }

void Editor::attach(void* parentView, int width, int height) {
    impl_->window.setWindowDimensions(static_cast<float>(width), static_cast<float>(height));
    impl_->window.show(parentView);
}

void Editor::detach() {
    if (impl_->window.isShowing()) impl_->window.close();
}

void Editor::setSize(int width, int height) { impl_->window.setWindowDimensionsFromHost(width, height); }

void Editor::renderToPng(const std::string& path, int width, int height) {
    impl_->window.setWindowless(width, height);
    impl_->window.takeScreenshot().save(path);
}

}
