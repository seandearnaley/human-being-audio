// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <hba/ui/controls.h>
#include <visage/app.h>
#include <visage_windowing/linux/windowing_x11.h>

#include <memory>

namespace {
struct ButtonContext {
    float scale = 1.0f;
    float px(float value) const { return value * scale; }
};

int grabPointer(Display* display) {
    return XGrabPointer(display, DefaultRootWindow(display), False, ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
                        None, None, CurrentTime);
}
} // namespace

TEST_CASE("An embedded export button hands its X11 pointer grab to the native file drag", "[file-drag]") {
    std::unique_ptr<Display, decltype(&XCloseDisplay)> host{XOpenDisplay(nullptr), XCloseDisplay};
    REQUIRE(host != nullptr);
    const auto parent = XCreateSimpleWindow(host.get(), DefaultRootWindow(host.get()), 0, 0, 160, 80, 0, 0, 0);
    XMapWindow(host.get(), parent);
    XSync(host.get(), False);

    ButtonContext context;
    visage::ApplicationWindow app;
    int clicks = 0;
    hba::ui::ActionButtonBase<ButtonContext> button(context, "Drag MIDI", [&] { ++clicks; });
    app.addChild(&button);
    button.setBounds(0, 0, 160, 80);
    app.show(160, 80, reinterpret_cast<void*>(parent));
    auto* window = dynamic_cast<visage::WindowX11*>(app.window());
    REQUIRE(window != nullptr);
    REQUIRE(grabPointer(window->x11Connection()->display()) == GrabSuccess);
    REQUIRE(grabPointer(host.get()) == AlreadyGrabbed);

    int hostGrab = -1;
    button.setOnDragOut([&] { hostGrab = grabPointer(host.get()); });
    visage::MouseEvent press;
    press.button_id = visage::kMouseButtonLeft;
    press.button_state = visage::kMouseButtonLeft;
    button.mouseDown(press);
    auto drag = press;
    drag.window_position = drag.position = {8.0f, 8.0f};
    button.mouseDrag(drag);
    CHECK(hostGrab == GrabSuccess);
    button.mouseUp(drag);
    CHECK(clicks == 0);
}
