// SPDX-License-Identifier: AGPL-3.0-or-later
#include "panels.h"
#include "controls.h"

namespace dt::ui {

Panel::Panel(Context& context, std::string title, unsigned accent)
    : context_(context), title_(std::move(title)), accent_(accent) {}

void Panel::draw(visage::Canvas& canvas) {
    drawPanel(canvas, context_, 0.0f, 0.0f, width(), height(), colour::panel, metric::radius);
    if (!title_.empty()) {
        const float sc = context_.scale;
        canvas.setColor(colour::withAlpha(accent_, 0.85f));
        canvas.roundedRectangle(10.0f * sc, 10.0f * sc, 3.0f * sc, 12.0f * sc, 1.5f * sc);
        drawLabel(canvas, context_, title_, 19.0f * sc, 7.0f * sc, width() - 28.0f * sc, 18.0f * sc, accent_,
                  visage::Font::kLeft, 9.8f, Weight::semibold);
    }
}

} // namespace dt::ui
