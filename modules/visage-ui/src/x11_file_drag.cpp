// SPDX-License-Identifier: GPL-3.0-or-later
#include <visage/app.h>
#include <visage_windowing/linux/windowing_x11.h>

namespace hba::ui {

void releasePointerForFileDrag(visage::Frame& source, const visage::MouseEvent& event) {
    auto* editor = source.findParent<visage::ApplicationEditor>();
    auto* window = editor ? dynamic_cast<visage::WindowX11*>(editor->window()) : nullptr;
    if (window == nullptr)
        return;

    // The press implicitly grabbed the pointer on Visage's X connection.
    // Finish that press before JUCE grabs the same device on its own connection.
    const auto position = window->convertToNative(event.window_position);
    window->handleMouseUp(visage::kMouseButtonLeft, position.x, position.y,
                          event.button_state & ~visage::kMouseButtonLeft, event.modifiers);
    auto* display = window->x11Connection()->display();
    XUngrabPointer(display, CurrentTime);
    XSync(display, False);
}

} // namespace hba::ui
